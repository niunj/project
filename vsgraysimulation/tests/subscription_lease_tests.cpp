#include <vsgraysim/broker/BrokerNet.h>
#include <vsgraysim/broker/EquipmentInfo.h>
#include "ShmTransport.h"
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>
using namespace vsgraysim;
using json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
json request(ipc::ShmTransport& client,const std::string& topic,const json& body){const auto bytes=body.dump();std::vector<uint8_t> answer;require(client.request(topic,bytes.data(),bytes.size(),answer,400),"lease transport request failed");return json::parse(answer);}
int main()try{
    std::string directory="/tmp/vsg_lease_test_XXXXXX";require(::mkdtemp(directory.data()),"cannot create IPC directory");::setenv("VSGRAYSIM_IPC_DIRECTORY",directory.c_str(),1);
    auto& net=BrokerNet::instance();BrokerNet::Config config;config.heartbeat_ms=50;config.lease_ms=800;net.startSimRole(config);
    EquipmentInfo equipment;equipment.equipment_id=4201;equipment.name="shared camera";equipment.type=SensorType::VIS;EquipmentRegistry::instance().registerEquipment(equipment);SimDataBroker::instance().serverOnline(true);
    ipc::ShmTransport a,b;a.init("lease_client_a");b.init("lease_client_b");require(a.start()&&b.start(),"cannot start subscriber transports");
    const auto scene=net.lastSimMeta().scene_session_id;
    auto attach=[&](ipc::ShmTransport& client,const std::string& id,bool data){auto req=json{{"client_id",id},{"equipment_id",4201},{"scene_session_id",scene},{"receive_frames",data}};auto answer=request(client,"broker/link/attach",req);require(answer.at("ok"),"attach rejected");req["token"]=answer.at("token");return req;};
    const auto first=attach(a,"operator_a",true);const auto duplicate=attach(a,"operator_a",true);require(first.at("token")==duplicate.at("token"),"retry created another lease");
    const auto second=attach(b,"operator_b",true);const auto binding=attach(a,"assembly",false);
    auto counts=net.subscriptionStatus().at(0);require(counts.at("data_subscribers")==2&&counts.at("binding_subscribers")==1,"lease ownership counts are wrong");
    auto invalid=first;invalid["token"]="other client's token";require(!request(a,"broker/link/detach",invalid).at("ok").get<bool>(),"foreign token detached a subscriber");
    invalid=first;invalid["scene_session_id"]="previous run";require(!request(a,"broker/link/detach",invalid).at("ok").get<bool>(),"stale scene detached live leases");
    require(request(a,"broker/link/detach",first).at("ok"),"normal detach failed");require(request(a,"broker/link/detach",first).at("ok"),"detach retry not idempotent");
    require(net.linkAllows(4201)&&SimDataBroker::instance().isSubscribed(4201),"one subscriber detached the other");
    require(net.subscriptionStatus().at(0).at("data_subscribers")==1,"detach removed another subscriber");
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    auto renewal=json{{"client_id","operator_b"},{"scene_session_id",scene},{"leases",json::array({{{"equipment_id",4201},{"token",second.at("token")}}})}};
    require(request(b,"broker/link/renew",renewal).at("valid").size()==1,"live lease did not renew");
    std::this_thread::sleep_for(std::chrono::milliseconds(450));require(net.linkAllows(4201),"renewed lease expired with another client");
    std::this_thread::sleep_for(std::chrono::milliseconds(650));require(!net.linkAllows(4201)&&!SimDataBroker::instance().isSubscribed(4201),"abandoned final subscriber did not expire");
    require(request(b,"broker/link/renew",renewal).at("valid").empty(),"expired lease resurrected without operator attach");
    const auto rebound=attach(b,"operator_b",true);require(rebound.at("token")!=second.at("token"),"reattach reused expired identity");
    net.clearLinks();require(!net.linkAllows(4201)&&net.subscriptionStatus().empty(),"scene reset retained subscriptions");require(!request(b,"broker/link/detach",rebound).at("ok").get<bool>(),"old scene command accepted after reset");
    json world={{"client_id","platform_console"},{"equipment_id",0},{"scene_session_id",net.lastSimMeta().scene_session_id},{"receive_frames",false},{"task_type","world"}};
    const auto worldReply=request(a,"broker/link/attach",world);require(worldReply.at("ok"),"time/platform task lease rejected");world["token"]=worldReply.at("token");require(net.hasTaskDemand()&&!net.linkAllows(0)&&!SimDataBroker::instance().isSubscribed(0),"world observer was treated as measurement equipment");
    auto invalidWorld=world;invalidWorld["receive_frames"]=true;require(!request(a,"broker/link/attach",invalidWorld).at("ok").get<bool>(),"world lease accepted a measurement subscription");require(request(a,"broker/link/detach",world).at("ok")&&!net.hasTaskDemand(),"world observer lease did not release");
    std::atomic<unsigned> routed{0};require(a.subscribe("test/owned",[&](const void*,size_t){++routed;}),"cannot subscribe routed topic");require(b.subscribe("test/other",[](const void*,size_t){}),"cannot subscribe unrelated topic");
    ipc::ShmTransport sender;sender.init("routing_source");require(sender.start(),"cannot start routing source");const std::string payload="actual payload";sender.publish("test/owned",payload.data(),payload.size());for(int n=0;n<100&&!routed;++n)std::this_thread::sleep_for(std::chrono::milliseconds(10));require(routed==1&&a.receivedBytes("test/owned")==payload.size(),"subscribed peer did not receive payload");require(b.receivedBytes("test/owned")==0,"unrelated peer received raw payload before filtering");a.unsubscribe("test/owned");sender.publish("test/owned",payload.data(),payload.size());std::this_thread::sleep_for(std::chrono::milliseconds(50));require(a.receivedBytes("test/owned")==payload.size(),"unsubscribed peer still received raw payload");sender.stop();
    a.stop();b.stop();net.stop();EquipmentRegistry::instance().clear();
    // A slow synchronous registry reply must not overwrite a newer heartbeat,
    // rewind the displayed time or revive the previous worker generation.
    ipc::ShmTransport publisher;publisher.init("ordered_snapshot_source");require(publisher.start(),"cannot start snapshot publisher");net.startDisplayRole(config);
    auto heartbeat=[&](uint64_t order,double time,const char* epoch){json value={{"proto",1},{"host","sim"},{"snapshot_order_ns",order},{"scene","test"},{"scene_session_id",epoch},{"session_id",epoch},{"sim_time",time},{"equipment",json::array()}};const auto bytes=value.dump();publisher.publish("broker/registry/heartbeat",bytes.data(),bytes.size());};
    auto awaitTime=[&](double time){for(int n=0;n<100&&net.lastSimMeta().sim_time!=time;++n)std::this_thread::sleep_for(std::chrono::milliseconds(10));require(net.lastSimMeta().sim_time==time,"snapshot did not arrive");};
    std::this_thread::sleep_for(std::chrono::milliseconds(100));heartbeat(100,10,"run_a");awaitTime(10);heartbeat(99,9,"run_a");std::this_thread::sleep_for(std::chrono::milliseconds(100));require(net.lastSimMeta().sim_time==10,"out of order reply rewound time");
    heartbeat(101,0,"run_b");awaitTime(0);heartbeat(100,10,"run_a");std::this_thread::sleep_for(std::chrono::milliseconds(100));require(net.lastSimMeta().scene_session_id=="run_b","stale heartbeat restored previous scene");
    net.stop();publisher.stop();std::filesystem::remove_all(directory);
    std::cout<<"PASS: idempotent leases, two clients, binding/data separation, token/epoch isolation, renewal, expiry and explicit reconnect\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';BrokerNet::instance().stop();return 1;}
