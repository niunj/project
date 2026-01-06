// Copyright 2006-2022 Sundog Software, LLC. All rights reserved worldwide.

/** \file ThreadCameraStreamData.h
\brief A class for describing and managing data/rendering in a particular thread and associated camera, stream
*/

#pragma once

#ifdef SWIG
%module SilverLiningTCSData
#define SILVERLINING_API
%{
#include "ThreadCameraStreamData.h"
using namespace SilverLining;
% }
#endif

#include "MemAlloc.h"
#include "Vector3.h"
#include "SilverLiningTypes.h"
#include "LocalTime.h"
#include "Location.h"
#include "LogLevel.h"
#include <functional>

namespace SilverLining
{
    // Forward declarations
    class Camera;
    class Renderer;

    // Forward declarations
    class BillboardRenderingParameters;
    class MetaballRenderingParameters;
    class VoxelRenderingParameters;

    class BillboardRenderer;

    class CloudRenderingParameters;
    class CumulusCloudRenderingParameters;

    class LuminanceMapper;

    class CloudLayer;

    class BillboardShader;

    class SkyShaders;
    class FlareShader;
    class CirrusShader;
    class StratusShader;
    class StratocumulusShaders;
    class CirroCumulusShader;
    class StarShader;
    class GlareShader;
    class AtmosphereFromSpaceShader;

    class CrepuscularRays;
    class Sky;

    class BillboardBufferProvider;

    class Atmosphere;

    class PrecipitationManager;
    class PrecipitationShaders;

    class TcsUserData;
    class MapUserDataImpl;
    class MapCloudLayerUserDataImpl;

    class ShadowMap;

    class TextureManager;

    class EnvironmentMap;

    class LensFlare;

    class RandomNumberGenerator;

    class TGALoader;

    /** This class is the main interface to thread safe rendering with SilverLining.

    The class represents the concept of thread local storage:
    https://en.wikipedia.org/wiki/Thread-local_storage

    The 'thread' part of the name represents each separate thread (and hence
    each instance of this class) that we wish to render from in a thread safe
    fashion. 

    The 'camera' part of the name represents each view(e.g. window/offscreen view) 
    that you wish to render from in a thread safe fashion in the corresponding thread.

    This typically also corresponds to an OpenGL context (you can share OpenGL contexts or not)
    or an immediate/deferred DirectX context. 

    The 'stream' part of the name represents the command stream (OpenGL/Vulkan) or the
    immediate/deferred context commands (DX 11, 11.1) being generated for a given thread, view.

    As an example, to render 3 views (one main view, and 2 offscreen views).
    
    Below are then the steps you follow to render everything in a thread safe fashion:

        - Instantiate 3 objects of this class. This has to be done where the OpenGL/DirectX context
        is active, and the atmosphere has already been created & initialized.
        
        - Set up the initial up and right vectors on the camera of each object by getting the camera associated 
        with this ThreadCameraStreamData (or tcsData for short), the same as the atmosphere 
        and also set up the view port of the camera (the size of the render target being rendered to). 
        
        - Initialize each object by first calling the first variant of the Initialize function
        that takes the renderer type. This must match the atmosphere renderer type that you initialized
        the atmosphere type with. The last parameter is important.
	
        - Then, you initialize each object by first calling the second variant of the Initialize function
        that takes the atmosphere that you wish to render in a thread safe fashion.

	    - You must optionally also set up the render target (an fbo in the case of OpenGL32)
	    if the tcsData in question, is going to be used to render into an offscreen view.

        - Next, you set up the individual cloud layers that will get rendered for each thread/camera (view)/stream:
            - Create a cloud layer.

            - Set up the cloud layer(s):
                  - Any methods that do not take the optional tcsData need be called once after creation.

                  - Any methods that take the optional tcsData parameter need to be called with each tcsData (so in
                   our case, 3 different sets of calls for each layer).

                  - Add the layer to the AtmosphericConditions like usual.

        - Following this, you can render like you normally do with a few changes:

            - Instead of setting the model view/projection matrix via the Atmosphere, you now
            set them on the camera object inside each tcs data (accessed using ThreadCameraStreamData::GetCamera), before rendering.

            - Pass in the tcsData pointer to Atmosphere::DrawSky, Atmosphere::DrawObjects.



    - A few notes:

        - This basic flow for rendering with ThreadCameraStreamData is all very similar to the existing
        steps/flow/API to render with SilverLining, and to that end we have also tried to minimize changes to the API and
        retained the existing API/way as much as possible. So, existing applications should work the same.

        - We recommend, however, that you use this interface/setup/flow to do all SilverLining rendering going forward.
        Specifically, we recommend manipulating multiple views and passing the view/projection matrices using
        instances of this class (by getting the camera) even if you don't require multi-threading. This way you decouple
        the data (Atmosphere) from the view in question and lend yourself easily to render/manipulate multiple views/channels/windows
        (including adding/removing windows/channels via instantiating/deleting the associated tcsData) 
        in a versatile & straightforward way. 

        - All the public interfaces now take an optional void* data parameter that pertains to a pointer of an object of this 
        class representing the thread/camera (view) /stream/ in question. 
     
        - This includes:
            - Atmosphere
            - AtmosphericConditions
            - CloudLayer
            - CloudLayerFactory (only for serializing/un-serializing)

        So make sure to pass the pointer to the object of the thread/camera(view)/stream in question in the same order/position/context
        of which thread/camera(view)/stream is in question. 

        - These are the only functions you need to use on ThreadCameraStreamData: 
            - The 2 initialization functions

            - GetCamera (and subsequently manipulate the view/projection)

            - ExecuteStream (see section on deferred rendering)



    - Multi-Threading:

        - Using this interface with these steps inherently makes silverlining completely thread safe
        for rendering each thread/camera (view)/stream. So in our example, you could render all 3 views simultaneously
        in a thread safe fashion. We did not have support for this before and users typically had to instantiate
        multiple atmospheres, etc to work around it.

        - However, you should not use this method to render using multiple OpenGL contexts, in multiple threads and switching them.
        Your performance will not improve, and in most cases degrade slightly, because the GPU will still serialize the calls internally
        and switching contexts (at least in OpenGL is expensive):
            - https://www.khronos.org/opengl/wiki/OpenGL_and_multithreading
            - https://www.khronos.org/opengl/wiki/OpenGL_Context
        - This brings us to the next point, deferred rendering.

    - Deferred Rendering:
    
        - A key observation of any rendering is that the actual work of generating commands for the GPU to execute and then 
        actually executing them on the GPU can effectively be decoupled.

        - Therefore, the command list generation for each different view can proceed in completely different threads.
        After all command lists for each view are generated, they can be rapidly executed on the GPU on the main thread.

        - This is where the last parameter to the ThreadCameraStreamData constructor comes into play. By passing deferred = true, you indicate
        that you wish to render (execute a stream/command list), after you have generate or 'collected' the draw calls.

        - With that parameter set to true, you can call DrawSky/DrawObjects in each thread simultaneously/concurrently 
        like usual with an important difference:
        This doesn't do any actual OpenGL/DX drawing, but simply does generation of drawing commands or 'command lists'.
      
        - The rendering context need not be active or current where you make these calls.

        - Following this, you 'execute' the command lists generated by each of the threads, in the main thread where the rendering context is active.

        - You do this by calling ThreadCameraStreamData::ExecuteStream for each of the tcsData objects associated with the threads.

        - This is all very similar to the way Sundog has support for multi-threaded in our Triton SDK:
        https://sundog-soft.com/2018/02/using-triton-4-0s-multi-threaded-rendering/

        - There are a few more interesting bits of general information (GPUs, multi-threading) there that are applicable to SilverLining
        as well, so its worthwhile reading that post.

        - The sequence of calls now for our example with 3 views is now:
         - Initialize all 3 tcsDatas in the main thread (with deferred parameter set to true for the constructor)
         - Each frame:
          - render each view in its own thread
           - view 0 in thread 0
             - Atmosphere::DrawSky()
             - Atmosphere::DrawObjects()

           - view 1 in thread 1
               - Atmosphere::DrawSky()
               - Atmosphere::DrawObjects()
           - view 2 in thread 2
               - Atmosphere::DrawSky()
               - Atmosphere::DrawObjects()

         - After all the views complete rendering:
           - join in the main thread:
           - call ThreadCameraStreamData::ExecuteStream for each view:
             - execute stream of view 0
             - execute stream of view 1
             - execute stream of view 2

        - There is a fully self-contained OpenGLMultiThreadedSample that illustrates all the above concepts & the API.

    !!NOTE!!
    - Deferred rendering and bindless rendering do not work concurrently right now.
      Therefore, be sure to set this option to no: cumulus-draw-bindless-indirect = no in the config or programatically
      via Atmosphere::SetConfigOption("cumulus-draw-bindless-indirect", "no")

    - Deferred rendering does not work if with occlusion queries that are (optionally) required for lens flare.
      Therefore, be sure to set this option to yes: lens-flare-disable-occlusion = yes in the config or programatically
      via Atmosphere::SetConfigOption("lens-flare-disable-occlusion", "yes")

    */

    class ThreadCameraStreamData : public MemObject
    {
    public:
        /** constructor
        \param rightHanded Set this the same as the initialized Atmosphere. \sa Atmosphere::Initialize()
        \param unitScale Set this the same as the initialized Atmosphere. \sa Atmosphere::GetUnitScale(), Atmosphere::SetWorldUnits()
        \param deferred Set whether this tcsData uses deferred rendering.
        */
        ThreadCameraStreamData(bool rightHanded, double unitScale, bool deferred);

        /** destructor*/
        virtual ~ThreadCameraStreamData();

    //! These are the only public methods you need to use to render multiple views
    //! using multiple threads with/without command lists.
    public:

        /** get the associated camera */
        const Camera* GetCamera(void) const;

        /** get the associated camera (non-const)*/
        Camera* GetCamera(void);

        /** Initialize the ThreadCameraStreamData (call first)
        \param rendererType The renderer type. The same as the type passed in to Atmosphere::Initialize

        \param rightHanded right handed or left handed system. The same as the type passed in to Atmosphere

        \param environment The same value as the one passed in to Atmosphere::Initialize

        \param avoidStalls If you with to do any combination of multi-threaded rendering and deferred
        you need to set this config option on the Atmosphere:
            atmosphere->SetConfigOption("avoid-opengl-stalls", "yes");
        and pass the same value (true)
        */
        int Initialize(int rendererType, bool rightHanded, void* environment, bool avoidStalls);

        /** OpenGL-only variant of ThreadCameraStreamData::Initialize() that takes in a list of user-compiled shader objects that will be 
        linked into all subsequently linked shader program objects. This allows you to inject your own shader functions into 
        our shaders without copying the source.
        \param rendererType The renderer type. The same as the type passed in to Atmosphere::Initialize

        \param rightHanded right handed or left handed system. The same as the type passed in to Atmosphere

        \param environment The same value as the one passed in to Atmosphere::Initialize

        \param avoidStalls If you with to do any combination of multi-threaded rendering and deferred 
        you need to set this config option on the Atmosphere: 
            atmosphere->SetConfigOption("avoid-opengl-stalls", "yes");
        and pass the same value (true)

        \param userShaders A list of user shaders, if any, to link into all shaders loaded by SilverLining.
        */
        int Initialize(int rendererType, bool rightHanded, void *environment, bool avoidStalls, const SL_VECTOR(unsigned int)& userShaders);

        /** Initialize the ThreadCameraStreamData for a given Atmosphere object (call 2nd)
        \param environment The atmosphere against which this tcsData is being initialized
        */
        void Initialize(const Atmosphere& atmosphere);

        /** Whether initialized*/
        bool IsInitialized(void) const;

        /** For OpenGL: Execute the command list for this object. See documentation above on 'deferred rendering'.
            For Vulkan: Execute one time/internal bookkeeping commands, if any, outside of the rendering/command buffer recording
            in a thread safe fashion (e.g. main thread), just before the submission of silverlining command buffers.
            See SilverLiningVulkanExample.
        */
        void ExecuteStream(void);

        /** Set the current command buffer that we are recording to (only for Vulkan renderer).
        * frameIndex is the image index/swap chain image index associated with the command buffer being recorded.
        * If it is not provided, SilverLining has to do some extra work for internal book keeping.
        */
        void SetStream(void* stream, int frameIndex = -1);

        /* Deinitialize. Call before deleting for each atmosphere initialized with*/
        void DeInitialize(const Atmosphere& atmosphere);

        /** Some effects (like lightning) need to be synchronized across multiple views.
        Re-seeding the atmosphere random number in these cases per view doesn't quite work right for those.
        To avoid this, set the value of tcs-data-effects-rng-unique to yes.
        Then, the tcs datas will use their own internal random generators in this case (which if seeded/reset the same,
        will synchronize the effects across the views.

        If an rng is not set via this call (and the tcs-data-effects-rng-unique is 'yes', the tcs data will use its
        own internal rng.
        */
        void SetRandomNumberGenerator(RandomNumberGenerator* rng);

        /** Get the random number generator. See SetRandomNumberGenerator*/
        RandomNumberGenerator* GetRandomNumberGenerator() const;

        /** Vulkan only.
        * If you intend to use environment maps via ThreadCameraStreamData::GetEnvironmentMap, this needs to be specified
        * before initializing ThreadCameraStreamData.
        */
        void SetEnvironmentMapFormat(ColorFormat format);

        /** Vulkan only.
        * If you intend to use shadow maps via ThreadCameraStreamData::GetShadowMap, this needs to be specified
        * before initializing ThreadCameraStreamData.
        */
        void SetUseShadowMap(bool val);

        /** Vulkan only.
        \param environment This must contain a pointer to a VulkanInitInfo structure, as defined in the VulkanInitInfo.h header.
        Only the parameters: renderPass, sampleCount, colorFormat and depthFormat are considered for the change.
        Other parameters are ignored.
        */
        void SettingsChanged(void* environment);

    //! !!!!Do Not Use!!!!
    //! !!!!Internal methods!!!!
    //! These should not be used by application developers!!
    public:

        //! set a user pointer and the user data associated with it
        void SetUserData(const void* userPointer, TcsUserData* userData);

        //! get a user pointer and the user data associated with it, if it exists
        //! null otherwise
        TcsUserData* GetUserData(const void* userPointer);

        //! whether user data exists for a user pointer
        bool UserDataExists(const void* userPointer) const;

        //! erase user data associated with this layer (just erases the entry)
        void EraseUserData(const void* userPointer);

        //! These are named variations of the 3 functions above

        //! set the name with userDataName as the key
        void SetCloudLayerTcsUserData(const CloudLayer* cloudLayer, TcsUserData* userData);

        //! get the name with userDataName as the key
        TcsUserData* GetCloudLayerTcsUserData(const CloudLayer* cloudLayer);

        //! exists?
        bool CloudLayerTcsUserDataExists(const CloudLayer* cloudLayer);
       
        //! erase user data associated with this layer (just erases the entry)
        void EraseCloudLayerTcsUserData(const CloudLayer* cloudLayer);
        
        //! internal. do not use directly
        Renderer* GetRenderer(void);

        //! internal. do not use directly
        const Renderer* GetRenderer(void) const;

        //! internal. Do not use directly. Get the parameters, const version
        const BillboardRenderingParameters* GetBillboardRenderingParams(void) const;

        //! internal. Do not use directly. Get the parameters, non-const version
        BillboardRenderingParameters* GetBillboardRenderingParams(void);

        //! internal. Do not use directly. Get the parameters, const version
        const MetaballRenderingParameters* GetMetaballRenderingParams(void) const;

        //! internal. Do not use directly. Get the parameters, non-const version
        MetaballRenderingParameters* GetMetaballRenderingParams(void);

        //! internal. Do not use directly. Get the billboard renderer, const version
        const BillboardRenderer* GetBillboardRenderer(void) const;

        //! internal. Do not use directly. Get the billboard renderer, non-const version
        BillboardRenderer* GetBillboardRenderer(void);

        //! internal. Do not use directly. Get the parameters, const version
        const CloudRenderingParameters* GetCloudRenderingParams(void) const;

        //! internal. Do not use directly. Get the parameters, non-const version
        CloudRenderingParameters* GetCloudRenderingParams(void);


        //! internal. Do not use directly. Get the parameters, const version
        const CumulusCloudRenderingParameters* GetCumulusCloudRenderingParams(void) const;

        //! internal. Do not use directly. Get the parameters, non-const version
        CumulusCloudRenderingParameters* GetCumulusCloudRenderingParams(void);

        //! internal. Do not use directly. Get the parameters, const version
        const LuminanceMapper* GetLuminanceMappper(void) const;

        //! internal. Do not use directly. Get the parameters, non-const version
        LuminanceMapper* GetLuminanceMappper(void);

        //! internal. Do not use directly. Whether drawing env map currently
        bool GetIsDrawingEnvMap() const;

        //! internal. Do not use directly. Set drawing env map to true/false
        void SetIsDrawingEnvMap(bool val);

        //! internal. Do not use directly. Whether drawing shadow map currently
        bool GetIsDrawingShadowMap() const;

        //! internal. Do not use directly. Set drawing shadow map to true/false
        void SetIsDrawingShadowMap(bool val);

        //! internal. Do not use directly. Get sort screen depth
        bool GetSortByScreenDepth(void) const;

        //! internal. Do not use directly. Set sort screen depth
        /** Set whether objects should be sorted by screen depth or distance from the view */
        void SetSortByScreenDepth(bool val);

        //! internal. do not use!
        const BillboardShader* GetBillboardShader(void) const;

        //! internal. do not use!
        const BillboardShader* GetBillboardShaderInstanced(void) const;

        //! internal. do not use!
        void ReloadShaders(void);


        //! internal. do not use!
        void Shutdown(void);

        //! internal. do not use!
        void DeviceLost(void);

        //! internal. do not use!
        void DeviceReset(void);

        //! internal. do not use!
        void FrameStarted();

        //! internal. do not use!
        void SetContext();

        //! internal. do not use!
        void ReloadBillboardShader(void);

        //! internal. do not use!
        const SkyShaders* GetSkyShaders(bool simpleShader) const;

        //! internal. do not use!
        void ReloadSkyShaders(void);

        //! internal. do not use!
        const StratocumulusShaders* GetStratocumulusShaders(void) const;

        //! internal. do not use!
        void ReloadStratocumulusShaders(void);

        //! internal. do not use!
        const FlareShader* GetFlareShader(void) const;

        //! internal. do not use!
        const CirrusShader* GetCirrusShader(void) const;

        //! internal. do not use!
        void ReloadCirrusShader(void);

        //! internal. do not use!
        const StratusShader* GetStratusShader(void) const;

        //! internal. do not use!
        void ReloadStratusShader(void);

        //! internal. do not use!
        const StarShader* GetStarShader(void) const;

        //! internal. do not use!
        void ReloadStarShader(void);

        //! internal. do not use!
        const GlareShader* GetGlareShader(void) const;

        //! internal. do not use!
        GlareShader* GetGlareShader(void);

        //! internal. do not use!
        const AtmosphereFromSpaceShader* GetAtmosphereFromSpaceShader(bool doShadow) const;

        //! get whether internal stream/context is a deferred stream
        //! call before initialize!
        bool GetDeferred(void) const;

        //! internal. do not use!
        CrepuscularRays* GetCrepuscularRays(void);

        //! internal. do not use!
        const CrepuscularRays* GetCrepuscularRays(void) const;

        //! internal. do not use!
        Sky* GetSky(void);

        //! internal. do not use!
        const Sky* GetSky(void) const;

        //! internal. do not use!
        LensFlare* GetLensFlare(void);

        //! internal. do not use!
        const LensFlare* GetLensFlare(void) const;

        const BillboardBufferProvider* GetBillboardBufferProvider(void) const;

        //! internal. do not use!
        bool GetIsDrawingSky(void) const;

        //! internal. do not use!
        bool GetIsDrawingObjects(void) const;

        //! internal. do not use!
        void SetIsDrawingSky(bool val);

        //! internal. do not use!
        void SetIsDrawingObjects(bool val);

        //! internal. do not use!
        void SetForceImmediate(bool val);

        //! internal. do not use!
        bool GetForceImmediate(void) const;

        //! internal. do not use!
        const PrecipitationShaders* GetPrecipitationShaders(void) const;

        //! internal. do not use!
        PrecipitationShaders* GetPrecipitationShaders(void);

        //! Helper function to set up the up and right vector on the Camera of this tcs data
        void SetUpAndRightVector(const Vector3& upVector, const Vector3& rightVector);

        //! internal. do not use!
        const PrecipitationManager* GetPrecipitationManager(void) const;

        //! internal. do not use!
        PrecipitationManager* GetPrecipitationManager(void);

        //! internal. do not use!
        void InitializePrecipitationManager(const Atmosphere& atmosphere);

        //! internal. do not use!
        bool GetPrecipitationEnabled(void) const;

        //! internal. do not use!
        const ShadowMap* GetOrCreateShadowMap(const Atmosphere& atmosphere) const;

        //! internal. do not use!
        ShadowMap* GetOrCreateShadowMap(const Atmosphere& atmosphere);

        //! internal. do not use!
        const ShadowMap* GetShadowMapObject(void) const;

        //! internal. do not use!
        int GetShadowMapDim() const;

        //! internal. do not use!
        void DeleteShadowMap(void);

        //! internal. do not use!
        void DeleteEnvironmentMaps(void);

        //! internal. do not use!
        const TextureManager* GetTextureManager(void) const;
        TextureManager* GetTextureManager(void);

        //! internal. do not use!
        void setCreateTextures(bool create);

        //! internal. do not use!
        bool GetEnvironmentMap(EnvironmentMap*& environmentMap, Atmosphere& atmosphere, void *&texture, int facesToRender = 6, bool floatingPoint = false, CameraHandle cameraID = 0,
            bool drawClouds = true, bool drawSunAndMoon = true, bool geocentricMode = false);

        //! internal. do not use!
        bool DrawIndirect() const;

        //! set infra red mode
        void SetInfraRedMode(bool bInfraRed);

        //! whether infra red  model
        bool GetInfraRedMode() const;
        //! internal. do not use!
        void SetTime(const LocalTime& pTime, unsigned long timerMilliseconds);

        //! internal. do not use!
        const LocalTime& GetTime(unsigned long timerMilliseconds);

        //! internal. do not use!
        void SetLocation(const Location& _location);

        //! internal. do not use!
        const Location& GetLocation() const;
        //! internal. do not use!
        Location& GetLocation();

        //! internal. do not use!
        void SetBaseTimeMS(unsigned long val);
        //! internal. do not use!

        unsigned long GetBaseTimeMS(void) const;

         //! internal. do not use!
        void EnableTimePassage(bool enabled, long relightFrequencyMS, unsigned long timerMilliseconds);

        bool WantsLightingUpdate(unsigned long timerMilliseconds);

        //! internal. These are used for serialization only
        LocalTime& GetTime();
        LocalTime& GetAdjustedTime();
        bool& GetTimePassage();

        //! internal helper
        bool GetHDREnabled() const;

        //! internal. do not use!
        //! creates if necessary
        const float* GetStratusMieLookupPixels(int& width);

        //! internal. do not use!
        //! creates if necessary
        const unsigned char* GetStratusNoisePixels(int& width, int& height);

        //! logging
        //! //! internal. do not use!
        std::ostream& log(LogLevel level);
    protected:
        //! internal
        void CreateStratusMieLookupPixels(void);        
    private:
        //! the stream (in OpenGL) or immediate/deferred context (in DirectX)
        //! command buffer for Vulkan
        void* stream = nullptr;
        //! the camera associated with this stream/context in this thread
        Camera* camera;
        //! the thread id, internal
        int threadId;

        //! map of a user pointer to user data
        MapUserDataImpl* mapImpl;

        //! map of a user data name to user data
        MapCloudLayerUserDataImpl* mapCloudLayerUserDataImpl;

        BillboardRenderingParameters* billboardParams;
        MetaballRenderingParameters* metaballParams;

        BillboardRenderer* billboardRenderer;

        mutable BillboardShader* billboardShader;

        mutable BillboardShader* billboardShaderInstanced;

        CloudRenderingParameters* cloudParams;

        CumulusCloudRenderingParameters* cumulusCloudParams;

        LuminanceMapper* luminanceMapper;

        bool drawingEnvMap;

        bool drawingShadowMap;

        bool sortScreenDepth;

        void* context;

        mutable SkyShaders* skyShaders;

        mutable StratocumulusShaders* stratoCumulusShaders;

        mutable FlareShader* flareShader;

        mutable CirrusShader* cirrusShader;

        mutable StratusShader* stratusShader;

        mutable StarShader* starShader;

        mutable GlareShader* glareShader;

        mutable AtmosphereFromSpaceShader* atmosphereFromSpaceShader;

        mutable PrecipitationShaders* precipitationShaders;

        Renderer* renderer;

        const bool deferred;

        CrepuscularRays* crepuscularRays;
        Sky* sky;

        BillboardBufferProvider* billboardBufferProvider;

        bool isDrawingSky;

        bool isDrawingObjects;

        bool forceImmediate;

        PrecipitationManager* precipitationManager;

        bool initialized;

        mutable ShadowMap* shadowMap;
        int shadowMapDim;

        TextureManager* textureManager;
        bool createTextures;

        SL_MAP(CameraHandle, EnvironmentMap*) environmentMapMap;

        LensFlare *lensFlare;

        RandomNumberGenerator* randomNumberGenerator;

        RandomNumberGenerator* defaultRandomNumberGenerator;

        bool infraRed;
        
        Location location;
        LocalTime localTime, adjustedTime;

        bool timePassage;
        unsigned long lastRelightPass, baseTimeMS;
        long relightFrequency;
        bool wantsLightingUpdate;

        float* mieLookupPixels;

        bool stratusTgaLoaded;
        TGALoader* stratusTgaLoader;

        ColorFormat environmentMapFormat = FORMAT_UNKNOWN;

        bool useShadowMap = false;

        LogLevel currentLogLevel = LogLevel::SL_LOG_LEVEL_WARN;
    };
}