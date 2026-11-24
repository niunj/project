#pragma once
#include "../env_modules/src/WeatherReflection.h"
// Exercise the production blend state on Vulkan, including an HDR constant
// radiance edge that must stay constant as cloud opacity changes.
inline void checkCloudEdges(rockyenv::sensor::GpuContext &context) {
    auto *device = context.device();
    double error = 0;
    for (bool reflection : {true, false}) {
        auto image = vsg::Image::create();
        image->imageType = VK_IMAGE_TYPE_2D;
        image->format = VK_FORMAT_R32G32B32A32_SFLOAT;
        image->extent = {4, 2, 1};
        image->mipLevels = 1;
        image->arrayLayers = 1;
        image->samples = VK_SAMPLE_COUNT_1_BIT;
        image->tiling = VK_IMAGE_TILING_OPTIMAL;
        image->usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        auto view = vsg::createImageView(device, image, VK_IMAGE_ASPECT_COLOR_BIT);
        vsg::RenderPass::Attachments attachments(1);
        auto &a = attachments[0];
        a.format = image->format;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        vsg::SubpassDescription sub;
        sub.colorAttachments = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        vsg::SubpassDependency dependency{};
        dependency.srcSubpass = 0;
        dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        auto pass = vsg::RenderPass::create(device, attachments, vsg::RenderPass::Subpasses{sub},
                                            vsg::RenderPass::Dependencies{dependency});
        auto render = vsg::RenderGraph::create();
        render->framebuffer = vsg::Framebuffer::create(pass, vsg::ImageViews{view}, 4, 2, 1);
        render->renderArea = {{0, 0}, {4, 2}};
        render->viewportState = vsg::ViewportState::create(VkExtent2D{4, 2});
        render->clearValues.resize(1);
        render->clearValues[0].color = {{0, 0, 0, 0}};
        const std::string vertex =
            "#version 450\nvoid main(){vec2 "
            "p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2)*2.-1.;gl_Position=vec4(p,0,1);}";
        for (bool cloud : {false, true}) {
            std::string fragment =
                "#version 450\nlayout(location=0)out vec4 color;void main(){vec3 sky=vec3(1.3,1.1,.7);";
            if (cloud)
                fragment += "float a=(gl_FragCoord.x-.5)/3.;vec3 "
                            "source=gl_FragCoord.y<1.?vec3(.4,.5,.8):sky;color=vec4(source" +
                            std::string(reflection ? "*a" : "") + ",a);}";
            else
                fragment += "color=vec4(sky," + std::string(reflection ? "0" : "1") + ");}";
            auto vs = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", vertex),
                 fs = vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", fragment);
            vsg::ShaderStages stages{vs, fs};
            require(vsg::ShaderCompiler::create()->compile(stages),
                    "reflection blend fixture compile failed");
            auto shader = vsg::ShaderSet::create(stages);
            auto cfg = vsg::GraphicsPipelineConfigurator::create(shader);
            auto depth = vsg::DepthStencilState::create();
            depth->depthTestEnable = VK_FALSE;
            depth->depthWriteEnable = VK_FALSE;
            auto blend = reflection ? rockyenv::weatherReflectionBlend() : rockyenv::weatherLayerBlend();
            auto raster = vsg::RasterizationState::create();
            raster->cullMode = VK_CULL_MODE_NONE;
            for (auto &state : cfg->pipelineStates) {
                if (state.cast<vsg::DepthStencilState>())
                    state = depth;
                else if (state.cast<vsg::ColorBlendState>())
                    state = blend;
                else if (state.cast<vsg::RasterizationState>())
                    state = raster;
            }
            shader->variants.emplace(cfg->shaderHints, shader->stages);
            cfg->init();
            auto draw = vsg::StateGroup::create();
            cfg->copyTo(draw);
            draw->addChild(vsg::Draw::create(3, 1, 0, 0));
            render->addChild(draw);
        }
        auto graph =
            vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(device),
                                      device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
        graph->addChild(render);
        context.compile(graph);
        context.execute(graph);
        std::array<vsg::vec4, 8> result;
        context.readbackBatch(
            {{view.get(), nullptr, VK_IMAGE_LAYOUT_GENERAL, sizeof(result), result.data()}});
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x) {
                const float alpha = x / 3.f;
                const vsg::vec3 sky(1.3, 1.1, .7), cloud = y ? sky : vsg::vec3(.4, .5, .8);
                const auto expected = sky * (1 - alpha) + cloud * alpha;
                const auto actual = result[y * 4 + x];
                error =
                    std::max(error, double(vsg::length(vsg::vec3(actual.x, actual.y, actual.z) - expected)));
                require(std::abs(actual.w - (reflection ? alpha : 1.f)) < 1e-6,
                        "reflection opacity was squared or background alpha leaked");
            }
    }
    require(error < 1e-6, "reflection produced an artificial bright edge or clipped HDR before composition");

    std::cout << "GPU cloud edge: straight and premultiplied HDR/opacity preserved; edge error=" << error
              << "\n";
}
