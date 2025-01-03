// clang-format off
#include <GL/glew.h> // GLEW has to be loaded before other GL libraries
// clang-format on

#include "IBL.h"
#include "illixr/data_format.hpp"
#include "illixr/error_util.hpp"
#include "illixr/extended_window.hpp"
#include "illixr/gl_util/obj.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/math_util.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/shader_util.hpp"
#include "illixr/shaders/demo_shader.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <array>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <camutils/Manipulator.h>
#include <chrono>
#include <cmath>
#include <eigen3/Eigen/Core>
#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/RenderTarget.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <fstream>
#include <future>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>
#include <iomanip>
#include <iostream>
#include <ktxreader/Ktx2Reader.h>
#include <math/mat4.h>
#include <math/quat.h>
#include <thread>
#include <utils/EntityManager.h>
#include <utils/Log.h>
#include <utils/NameComponentManager.h>
#include <utils/Path.h>

using namespace ILLIXR;

// Wake up 1 ms after vsync instead of exactly at vsync to account for scheduling uncertainty
static constexpr std::chrono::milliseconds VSYNC_SAFETY_DELAY{1};

class filament_gldemo : public threadloop {
public:
    // Public constructor, create_component passes Switchboard handles ("plugs")
    // to this constructor. In turn, the constructor fills in the private
    // references to the switchboard plugs, so the component can read the
    // data whenever it needs to.

    filament_gldemo(const std::string& name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , xwin{new xlib_gl_extended_window{1, 1, pb->lookup_impl<xlib_gl_extended_window>()->glc}}
        , sb{pb->lookup_impl<switchboard>()}
        , pp{pb->lookup_impl<pose_prediction>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_vsync{sb->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")}
        , _m_image_handle{sb->get_writer<image_handle>("image_handle")}
        , _m_eyebuffer{sb->get_writer<rendered_frame>("eyebuffer")} {
        spdlogger(std::getenv("GLDEMO_LOG_LEVEL"));
    }

    // Essentially, a crude equivalent of XRWaitFrame.
    void wait_vsync() {
        switchboard::ptr<const switchboard::event_wrapper<time_point>> next_vsync = _m_vsync.get_ro_nullable();
        time_point                                                     now        = _m_clock->now();
        time_point                                                     wait_time{};

        if (next_vsync == nullptr) {
            // If no vsync data available, just sleep for roughly a vsync period.
            // We'll get synced back up later.
            std::this_thread::sleep_for(display_params::period);
            return;
        }

#ifndef NDEBUG
        if (log_count > LOG_PERIOD) {
            double vsync_in = duration2double<std::milli>(**next_vsync - now);
            spdlog::get(name)->debug("First vsync is in {} ms", vsync_in);
        }
#endif

        bool hasRenderedThisInterval = (now - lastTime) < display_params::period;

        // If less than one frame interval has passed since we last rendered...
        if (hasRenderedThisInterval) {
            // We'll wait until the next vsync, plus a small delay time.
            // Delay time helps with some inaccuracies in scheduling.
            wait_time = **next_vsync + VSYNC_SAFETY_DELAY;

            // If our sleep target is in the past, bump it forward
            // by a vsync period, so it's always in the future.
            while (wait_time < now) {
                wait_time += display_params::period;
            }

#ifndef NDEBUG
            if (log_count > LOG_PERIOD) {
                double wait_in = duration2double<std::milli>(wait_time - now);
                spdlog::get(name)->debug("Waiting until next vsync, in {} ms", wait_in);
            }
#endif
            // Perform the sleep.
            // TODO: Consider using Monado-style sleeping, where we nanosleep for
            // most of the wait, and then spin-wait for the rest?
            std::this_thread::sleep_for(wait_time - now);
        } else {
#ifndef NDEBUG
            if (log_count > LOG_PERIOD) {
                spdlog::get(name)->debug("We haven't rendered yet, rendering immediately");
            }
#endif
        }
    }

    void _p_thread_setup() override {
        lastTime = _m_clock->now();

        // Note: glXMakeContextCurrent must be called from the thread which will be using it.
        [[maybe_unused]] const bool gl_result = static_cast<bool>(glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc));
        assert(gl_result && "glXMakeCurrent should not fail");

        // Initialize filament objects
        engine    = filament::Engine::create(filament::Engine::Backend::OPENGL, nullptr, xwin->glc);
        swapChain = engine->createSwapChain((uint32_t) display_params::width_pixels, (uint32_t) display_params::height_pixels);
        // swapChain = engine->createSwapChain((void*)&xwin->win);
        renderer = engine->createRenderer();
        // Create Filament camera
        utils::EntityManager& em = utils::EntityManager::get();
        em.create(1, &eyeCameraEntity);
        eyeCamera = engine->createCamera(eyeCameraEntity);
        eyeCamera->setProjection(display_params::fov_x, (double) display_params::width_pixels / display_params::height_pixels,
                                 rendering_params::near_z, rendering_params::far_z, filament::Camera::Fov::HORIZONTAL);

        view = engine->createView();
        view->setName("Main View");
        view->setViewport({0, 0, display_params::width_pixels, display_params::height_pixels});
        // Create scene
        scene = engine->createScene();
        view->setVisibleLayers(0x4, 0x4);
        std::string iblDir = obj_dir + "/ibl/kloppenheim";
        utils::Path iblPath(iblDir);
        if (!iblPath.exists()) {
            std::cerr << "The specified IBL path does not exist: " << iblPath << std::endl;
        }
        ibl = std::make_unique<IBL>(*engine);
        if (!ibl->loadFromDirectory(iblPath)) {
            std::cerr << "Could not load the specified IBL: " << iblPath << std::endl;
            ibl.reset(nullptr);
        }
        ibl->getSkybox()->setLayerMask(0x7, 0x4);
        auto indirectLight = ibl->getIndirectLight();
        indirectLight->setIntensity(20000);
        indirectLight->setRotation(filament::math::mat3f::rotation(0.5f, filament::math::float3{0, 1, 0}));
        scene->setSkybox(ibl->getSkybox());
        scene->setIndirectLight(ibl->getIndirectLight());
        view->setScene(scene);
        filament::ScreenSpaceReflectionsOptions ssr;
        ssr.enabled = true;
        view->setScreenSpaceReflectionsOptions(ssr);
        view->setAntiAliasing(filament::View::AntiAliasing::NONE);
        view->setMultiSampleAntiAliasingOptions({.enabled = true, .sampleCount = 4});
        view->setAmbientOcclusionOptions({.quality = filament::QualityLevel::ULTRA, .enabled = true});
        // Load glTF assets
        names       = new utils::NameComponentManager(utils::EntityManager::get());
        materials   = filament::gltfio::createJitShaderProvider(engine);
        assetLoader = filament::gltfio::AssetLoader::create({engine, materials, names});

        for (int i = 0; i < numOfAssets; i++) {
            loadAsset(assetPaths[i], &filamentAssets[i], &filamentInstances[i], assetLoader);
            loadResources(assetPaths[i], &resourceLoaders[i], engine, &stbDecoder, &ktxDecoder, filamentAssets[i],
                          filamentInstances[i]);
        }

        // Add sun
        const utils::Entity*    entities = filamentAssets[0]->getEntities();
        filament::LightManager& lm       = engine->getLightManager();
        for (int i = 0; i < filamentAssets[0]->getEntityCount(); i++) {
            if (strcmp(filamentAssets[0]->getName(entities[i]), "SUN") == 0) {
                std::cerr << "Found SUN\n";
                filament::LightManager::Builder(filament::LightManager::Type::SUN)
                    .color(filament::math::float3(1.0, 1.0, 1.0))
                    .intensity(100000.0f)
                    .direction(normalize(lm.getDirection(lm.getInstance(entities[i]))))
                    .castShadows(true)
                    .sunAngularRadius(1.0f)
                    .sunHaloSize(10.0f)
                    .sunHaloFalloff(240.0f)
                    .build(*engine, entities[i]);
                scene->addEntity(entities[i]);
            } else {
                filament::LightManager::Instance linstance = lm.getInstance(entities[i]);
                if (lm.getType(linstance) == filament::LightManager::Type::POINT &&
                    strcmp(filamentAssets[0]->getName(entities[i]), "HDRI_SKY")) {
                    auto&                                 tm  = engine->getTransformManager();
                    auto                                  pos = tm.getWorldTransform(tm.getInstance(entities[i]));
                    filament::LightManager::ShadowOptions shadowOps;
                    shadowOps.mapSize = 2048;
                    filament::LightManager::Builder(filament::LightManager::Type::POINT)
                        .color(lm.getColor(lm.getInstance(entities[i])))
                        .intensity(50000.0f, filament::LightManager::EFFICIENCY_LED)
                        .position(filament::math::float3(pos[0][3], pos[1][3], pos[2][3]))
                        .falloff(5)
                        .castShadows(true)
                        .shadowOptions(shadowOps)
                        .build(*engine, entities[i]);
                    scene->addEntity(entities[i]);
                }
            }
        }

        // Create filament textures
        createSharedEyebuffer(&eyeTextureHandles[0][0]);
        _m_image_handle.put(
            _m_image_handle.allocate<image_handle>(image_handle{eyeTextureHandles[0][0], 1, swapchain_usage::LEFT_SWAPCHAIN}));
        createSharedEyebuffer(&eyeTextureHandles[0][1]);
        _m_image_handle.put(
            _m_image_handle.allocate<image_handle>(image_handle{eyeTextureHandles[0][1], 1, swapchain_usage::RIGHT_SWAPCHAIN}));
        createSharedEyebuffer(&eyeTextureHandles[1][0]);
        _m_image_handle.put(
            _m_image_handle.allocate<image_handle>(image_handle{eyeTextureHandles[1][0], 1, swapchain_usage::LEFT_SWAPCHAIN}));
        createSharedEyebuffer(&eyeTextureHandles[1][1]);
        _m_image_handle.put(
            _m_image_handle.allocate<image_handle>(image_handle{eyeTextureHandles[1][1], 1, swapchain_usage::RIGHT_SWAPCHAIN}));

        eyeTextures[0][0] = filament::Texture::Builder()
                                .width(display_params::width_pixels)
                                .height(display_params::height_pixels)
                                .levels(1)
                                .usage(filament::Texture::Usage::COLOR_ATTACHMENT | filament::Texture::Usage::SAMPLEABLE)
                                .format(filament::Texture::InternalFormat::RGB8)
                                .import(eyeTextureHandles[0][0])
                                .build(*engine);
        eyeTextures[0][1] = filament::Texture::Builder()
                                .width(display_params::width_pixels)
                                .height(display_params::height_pixels)
                                .levels(1)
                                .usage(filament::Texture::Usage::COLOR_ATTACHMENT | filament::Texture::Usage::SAMPLEABLE)
                                .format(filament::Texture::InternalFormat::RGB8)
                                .import(eyeTextureHandles[0][1])
                                .build(*engine);
        eyeTextures[1][0] = filament::Texture::Builder()
                                .width(display_params::width_pixels)
                                .height(display_params::height_pixels)
                                .levels(1)
                                .usage(filament::Texture::Usage::COLOR_ATTACHMENT | filament::Texture::Usage::SAMPLEABLE)
                                .format(filament::Texture::InternalFormat::RGB8)
                                .import(eyeTextureHandles[1][0])
                                .build(*engine);
        eyeTextures[1][1] = filament::Texture::Builder()
                                .width(display_params::width_pixels)
                                .height(display_params::height_pixels)
                                .levels(1)
                                .usage(filament::Texture::Usage::COLOR_ATTACHMENT | filament::Texture::Usage::SAMPLEABLE)
                                .format(filament::Texture::InternalFormat::RGB8)
                                .import(eyeTextureHandles[1][1])
                                .build(*engine);
        depthTexture = filament::Texture::Builder()
                           .width(display_params::width_pixels)
                           .height(display_params::height_pixels)
                           .levels(1)
                           .usage(filament::Texture::Usage::DEPTH_ATTACHMENT)
                           .format(filament::Texture::InternalFormat::DEPTH24)
                           .build(*engine);
        renderTargets[0][0] = filament::RenderTarget::Builder()
                                  .texture(filament::RenderTarget::AttachmentPoint::COLOR, eyeTextures[0][0])
                                  .texture(filament::RenderTarget::AttachmentPoint::DEPTH, depthTexture)
                                  .build(*engine);
        renderTargets[0][1] = filament::RenderTarget::Builder()
                                  .texture(filament::RenderTarget::AttachmentPoint::COLOR, eyeTextures[0][1])
                                  .texture(filament::RenderTarget::AttachmentPoint::DEPTH, depthTexture)
                                  .build(*engine);
        renderTargets[1][0] = filament::RenderTarget::Builder()
                                  .texture(filament::RenderTarget::AttachmentPoint::COLOR, eyeTextures[1][0])
                                  .texture(filament::RenderTarget::AttachmentPoint::DEPTH, depthTexture)
                                  .build(*engine);
        renderTargets[1][1] = filament::RenderTarget::Builder()
                                  .texture(filament::RenderTarget::AttachmentPoint::COLOR, eyeTextures[1][1])
                                  .texture(filament::RenderTarget::AttachmentPoint::DEPTH, depthTexture)
                                  .build(*engine);
        view->setRenderTarget(renderTargets[0][0]);
    }

    void _p_one_iteration() override {
        for (int i = 0; i < numOfAssets; i++) {
            if (resourceLoaders[i])
                resourceLoaders[i]->asyncUpdateLoad();
            populateScene(filamentAssets[i], scene);
        }
        // Essentially, XRWaitFrame.
        wait_vsync();

        Eigen::Matrix4f modelMatrix = Eigen::Matrix4f::Identity();
        Eigen::Vector3f scale(0.015, 0.015, 0.015);
        modelMatrix.row(0) *= scale.x();
        modelMatrix.row(1) *= scale.y();
        modelMatrix.row(2) *= scale.z();

        const fast_pose_type fast_pose = pp->get_fast_pose();
        pose_type            pose      = fast_pose.pose;

        Eigen::Matrix3f head_rotation_matrix = pose.orientation.toRotationMatrix();

        for (auto eye_idx = 0; eye_idx < 2; eye_idx++) {
            // Offset of eyeball from pose
            auto eyeball = Eigen::Vector3f((eye_idx == 0 ? -display_params::ipd / 2.0f : display_params::ipd / 2.0f), 0, 0);

            // Apply head rotation to eyeball offset vector
            eyeball = head_rotation_matrix * eyeball;

            // Apply head position to eyeball
            eyeball += pose.position;
            eyeball += offset;
            // Build our eye matrix from the pose's position + orientation.
            Eigen::Matrix4f eye_matrix   = Eigen::Matrix4f::Identity();
            eye_matrix.block<3, 1>(0, 3) = eyeball; // Set position to eyeball's position
            eye_matrix.block<3, 3>(0, 0) = pose.orientation.toRotationMatrix();

            // Objects' "view matrix" is inverse of eye matrix.
            filament::math::mat4 cameraMatrix(eye_matrix(0, 0), eye_matrix(1, 0), eye_matrix(2, 0), eye_matrix(3, 0),
                                              eye_matrix(0, 1), eye_matrix(1, 1), eye_matrix(2, 1), eye_matrix(3, 1),
                                              eye_matrix(0, 2), eye_matrix(1, 2), eye_matrix(2, 2), eye_matrix(3, 2),
                                              eye_matrix(0, 3), eye_matrix(1, 3), eye_matrix(2, 3), eye_matrix(3, 3));

            eyeCamera->setModelMatrix(cameraMatrix);
            view->setCamera(eyeCamera);
            view->setRenderTarget(renderTargets[which_buffer][eye_idx]);

            if (renderer->beginFrame(swapChain)) {
                renderer->render(view);
                renderer->endFrame();
            } else {
                renderer->render(view);
                renderer->endFrame();
            }
        }

        // glFinish();

#ifndef NDEBUG
        const double frame_duration_s = duration2double(_m_clock->now() - lastTime);
        const double fps              = 1.0 / frame_duration_s;

        if (log_count > LOG_PERIOD) {
            spdlog::get(name)->debug("Submitting frame to buffer {}, frametime: {}, FPS: {}", which_buffer, frame_duration_s,
                                     fps);
        }
#endif
        lastTime = _m_clock->now();

        /// Publish our submitted frame index to Switchboard!
        _m_eyebuffer.put(_m_eyebuffer.allocate<rendered_frame>(
            rendered_frame{std::array<GLuint, 2>{which_buffer, which_buffer}, std::array<GLuint, 2>{which_buffer, which_buffer},
                           fast_pose, fast_pose.predict_computed_time, lastTime}));

        which_buffer = (which_buffer + 1) % 2;

#ifndef NDEBUG
        if (log_count > LOG_PERIOD) {
            log_count = 0;
        } else {
            log_count++;
        }
#endif
    }

#ifndef NDEBUG
    size_t log_count  = 0;
    size_t LOG_PERIOD = 20;
#endif

private:
    const std::unique_ptr<const xlib_gl_extended_window>              xwin;
    const std::shared_ptr<switchboard>                                sb;
    const std::shared_ptr<pose_prediction>                            pp;
    const std::shared_ptr<const RelativeClock>                        _m_clock;
    const switchboard::reader<switchboard::event_wrapper<time_point>> _m_vsync;

    // Switchboard plug for application eye buffer.
    // We're not "writing" the actual buffer data,
    // we're just atomically writing the handle to the
    // correct eye/framebuffer in the "swapchain".
    switchboard::writer<image_handle>   _m_image_handle;
    switchboard::writer<rendered_frame> _m_eyebuffer;
    // Filament objects
    filament::Engine*    engine;
    filament::View*      view;
    filament::SwapChain* swapChain;
    filament::Renderer*  renderer;
    utils::Entity        eyeCameraEntity;
    filament::Camera*    eyeCamera;
    filament::Scene*     scene;

    // These are for loading assets
    filament::gltfio::AssetLoader*      assetLoader;
    filament::gltfio::MaterialProvider* materials;
    utils::NameComponentManager*        names;
    std::vector<utils::Path>            assetPaths;

    int             numOfAssets;
    std::string     obj_dir;
    Eigen::Vector3f offset;

    std::array<filament::gltfio::FilamentAsset*, 2>    filamentAssets    = {nullptr};
    std::array<filament::gltfio::FilamentInstance*, 2> filamentInstances = {nullptr};
    std::array<filament::gltfio::ResourceLoader*, 2>   resourceLoaders   = {nullptr};
    filament::gltfio::TextureProvider*                 stbDecoder        = nullptr;
    filament::gltfio::TextureProvider*                 ktxDecoder        = nullptr;
    // Image based lighting
    std::unique_ptr<IBL> ibl;

    // Filament textures for color and depth
    std::array<std::array<filament::Texture*, 2>, 2> eyeTextures;
    filament::Texture*                               depthTexture;
    // This is basically a framebuffer
    std::array<std::array<filament::RenderTarget*, 2>, 2> renderTargets;
    // Texture handles for publishing the rendered textures
    std::array<std::array<GLuint, 2>, 2> eyeTextureHandles;

    unsigned char which_buffer = 0;

    time_point lastTime{};

    static std::ifstream::pos_type getFileSize(const char* filename) {
        std::ifstream in(filename, std::ifstream::ate | std::ifstream::binary);
        return in.tellg();
    }

    void loadAsset(utils::Path filename, filament::gltfio::FilamentAsset** asset, filament::gltfio::FilamentInstance** instance,
                   filament::gltfio::AssetLoader* assetLoader) {
        // Peek at the file size to allow pre-allocation.
        long contentSize = static_cast<long>(getFileSize(filename.c_str()));
        if (contentSize <= 0) {
            std::cerr << "Unable to open " << filename << std::endl;
            exit(1);
        }

        // Consume the glTF file.
        std::ifstream        in(filename.c_str(), std::ifstream::binary | std::ifstream::in);
        std::vector<uint8_t> buffer(static_cast<unsigned long>(contentSize));
        if (!in.read((char*) buffer.data(), contentSize)) {
            std::cerr << "Unable to read " << filename << std::endl;
            exit(1);
        }

        // Parse the glTF file and create Filament entities.
        *asset    = assetLoader->createAsset(buffer.data(), buffer.size());
        *instance = (*asset)->getInstance();
        buffer.clear();
        buffer.shrink_to_fit();

        if (!*asset) {
            std::cerr << "Unable to parse " << filename << std::endl;
            exit(1);
        }
    }

    void loadResources(utils::Path filename, filament::gltfio::ResourceLoader** resourceLoader, filament::Engine* engine,
                       filament::gltfio::TextureProvider** stbDecoder, filament::gltfio::TextureProvider** ktxDecoder,
                       filament::gltfio::FilamentAsset* asset, filament::gltfio::FilamentInstance* instance) {
        // Load external textures and buffers.
        std::string                             gltfPath      = filename.getAbsolutePath();
        filament::gltfio::ResourceConfiguration configuration = {};
        configuration.engine                                  = engine;
        configuration.gltfPath                                = gltfPath.c_str();
        configuration.normalizeSkinningWeights                = true;

        if (!(*resourceLoader)) {
            *resourceLoader = new filament::gltfio::ResourceLoader(configuration);
            *stbDecoder     = filament::gltfio::createStbProvider(engine);
            *ktxDecoder     = filament::gltfio::createKtx2Provider(engine);
            (*resourceLoader)->addTextureProvider("image/png", *stbDecoder);
            (*resourceLoader)->addTextureProvider("image/jpeg", *stbDecoder);
            (*resourceLoader)->addTextureProvider("image/ktx2", *ktxDecoder);
        }

        if (!((*resourceLoader)->asyncBeginLoad(asset))) {
            std::cerr << "Unable to start loading resources for " << filename << std::endl;
            exit(1);
        }

        asset->releaseSourceData();

        // Enable stencil writes on all material instances.
        const size_t                             matInstanceCount = instance->getMaterialInstanceCount();
        filament::MaterialInstance* const* const instances        = instance->getMaterialInstances();
        for (int mi = 0; mi < matInstanceCount; mi++) {
            instances[mi]->setStencilWrite(true);
            instances[mi]->setStencilOpDepthStencilPass(filament::MaterialInstance::StencilOperation::INCR);
        }
    }

    void populateScene(filament::gltfio::FilamentAsset* asset, filament::Scene* scene) {
        static constexpr int kNumAvailable = 128;
        utils::Entity        renderables[kNumAvailable];
        while (size_t numWritten = asset->popRenderables(renderables, kNumAvailable)) {
            printf("Adding an entity\n");
            scene->addEntities(renderables, numWritten);
        }
    }

    static void createSharedEyebuffer(GLuint* texture_handle) {
        // Create the shared eye texture handle
        glGenTextures(1, texture_handle);
        glBindTexture(GL_TEXTURE_2D, *texture_handle);

        // Set the texture parameters for the texture that the FBO will be mapped into
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, display_params::width_pixels, display_params::height_pixels, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, nullptr);

        // Unbind texture
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void createFBO(const GLuint* texture_handle, GLuint* fbo, GLuint* depth_target) {
        // Create a framebuffer to draw some things to the eye texture
        glGenFramebuffers(1, fbo);

        // Bind the FBO as the active framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
        glGenRenderbuffers(1, depth_target);
        glBindRenderbuffer(GL_RENDERBUFFER, *depth_target);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, display_params::width_pixels, display_params::height_pixels);
        // glRenderbufferStorageMultisample(GL_RENDERBUFFER, fboSampleCount, GL_DEPTH_COMPONENT, display_params::width_pixels,
        // display_params::height_pixels);

        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        // Bind eyebuffer texture
        spdlog::get(name)->info("About to bind eyebuffer texture, texture handle: {}", *texture_handle);

        glBindTexture(GL_TEXTURE_2D, *texture_handle);
        glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *texture_handle, 0);
        glBindTexture(GL_TEXTURE_2D, 0);

        // attach a renderbuffer to depth attachment point
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth_target);

        // Unbind FBO
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

public:
    // We override start() to control our own lifecycle
    void start() override {
        [[maybe_unused]] const bool gl_result_0 = static_cast<bool>(glXMakeCurrent(xwin->dpy, xwin->win, xwin->glc));
        assert(gl_result_0 && "glXMakeCurrent should not fail");

        // Init and verify GLEW
        const GLenum glew_err = glewInit();
        if (glew_err != GLEW_OK) {
            spdlog::get(name)->error("GLEW Error: {}", glewGetErrorString(glew_err));
            ILLIXR::abort("Failed to initialize GLEW");
        }

        numOfAssets = 1;
        // modify the asset path appropriately!!
        // Load/initialize the demo scene
        char* env_var = std::getenv("ILLIXR_DEMO_DATA");
        if (env_var == nullptr) {
            ILLIXR::abort("Please define ILLIXR_DEMO_DATA(the `assets` directory).");
        }
        obj_dir    = std::string(env_var);
        assetPaths = {
            // utils::Path("/opt/ILLIXR/assets/main_sponza/main/NewSponza_Main_glTF_002.gltf"),
            utils::Path(std::string(obj_dir) + "/main_sponza/main/NewSponza_Main_glTF_002.gltf"),
        };
        offset = Eigen::Vector3f(0, 4, 0);

        [[maybe_unused]] const bool gl_result_1 = static_cast<bool>(glXMakeCurrent(xwin->dpy, None, nullptr));
        assert(gl_result_1 && "glXMakeCurrent should not fail");

        // Effectively, last vsync was at zero.
        // Try to run filament_gldemo right away.
        threadloop::start();
    }
};

PLUGIN_MAIN(filament_gldemo)
