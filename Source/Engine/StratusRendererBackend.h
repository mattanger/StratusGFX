
#ifndef STRATUSGFX_RENDERER_H
#define STRATUSGFX_RENDERER_H

#include <string>
#include <vector>
#include <list>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "StratusEntity.h"
#include "StratusCommon.h"
#include "StratusCamera.h"
#include "StratusTexture.h"
#include "StratusFrameBuffer.h"
#include "StratusLight.h"
#include "StratusMath.h"
#include "StratusGpuBuffer.h"
#include "StratusGpuCommon.h"
#include "StratusThread.h"
#include "StratusAsync.h"
#include "StratusEntityCommon.h"
#include "StratusEntity.h"
#include "StratusTransformComponent.h"
#include "StratusRenderComponents.h"

namespace stratus {
    class Pipeline;
    class Light;
    class InfiniteLight;
    class Quad;
    struct PostProcessFX;

    extern bool IsRenderable(const EntityPtr&);
    extern bool IsLightInteracting(const EntityPtr&);
    extern size_t GetMeshCount(const EntityPtr&);

    ENTITY_COMPONENT_STRUCT(MeshWorldTransforms)
        MeshWorldTransforms() = default;
        MeshWorldTransforms(const MeshWorldTransforms&) = default;

        std::vector<glm::mat4> transforms;
    };

    struct RenderMeshContainer {
        RenderComponent * render = nullptr;
        MeshWorldTransforms * transform = nullptr;
        size_t meshIndex = 0;
    };

    typedef std::shared_ptr<RenderMeshContainer> RenderMeshContainerPtr;

    // struct RendererEntityData {
    //     std::vector<glm::mat4> modelMatrices;
    //     std::vector<glm::vec3> diffuseColors;
    //     std::vector<glm::vec3> baseReflectivity;
    //     std::vector<float> roughness;
    //     std::vector<float> metallic;
    //     GpuArrayBuffer buffers;
    //     size_t size = 0;    
    //     // if true, regenerate buffers
    //     bool dirty;
    // };

    struct RendererMouseState {
        int32_t x;
        int32_t y;
        uint32_t mask;
    };

    typedef std::unordered_map<EntityPtr, std::vector<RenderMeshContainerPtr>> EntityMeshData;

    struct RendererCascadeData {
        // Use during shadow map rendering
        glm::mat4 projectionViewRender;
        // Use during shadow map sampling
        glm::mat4 projectionViewSample;
        // Transforms from a cascade 0 sampling coordinate to the current cascade
        glm::mat4 sampleCascade0ToCurrent;
        glm::vec4 cascadePlane;
        glm::vec3 cascadePositionLightSpace;
        glm::vec3 cascadePositionCameraSpace;
        float cascadeRadius;
        float cascadeBegins;
        float cascadeEnds;
    };

    struct RendererCascadeContainer {
        FrameBuffer fbo;
        std::vector<RendererCascadeData> cascades;
        glm::vec4 cascadeShadowOffsets[2];
        uint32_t cascadeResolutionXY;
        InfiniteLightPtr worldLight;
        CameraPtr worldLightCamera;
        glm::vec3 worldLightDirectionCameraSpace;
        bool regenerateFbo;    
    };

    struct RendererMaterialInformation {
        size_t maxMaterials = 2048;
        // These are the materials we draw from to calculate the material-indices map
        std::unordered_set<MaterialPtr> availableMaterials;
        // Indices can change completely if new materials are added
        std::unordered_map<MaterialPtr, uint32_t> indices;
        // List of CPU-side materials for easy copy to GPU
        std::vector<GpuMaterial> materials;
        GpuBuffer materialsBuffer;
    };

    struct LightUpdateQueue {
        template<typename LightPtrContainer>
        void PushBackAll(const LightPtrContainer& container) {
            for (const LightPtr& ptr : container) {
                PushBack(ptr);
            }
        }

        void PushBack(const LightPtr& ptr) {
            if (existing_.find(ptr) != existing_.end() || !ptr->CastsShadows()) return;
            queue_.push_back(ptr);
            existing_.insert(ptr);
        }

        LightPtr PopFront() {
            if (Size() == 0) return nullptr;
            auto front = Front();
            existing_.erase(front);
            queue_.pop_front();
            return front;
        }

        LightPtr Front() const {
            if (Size() == 0) return nullptr;
            return queue_.front();
        }

        // In case a light needs to be removed without being updated
        void Erase(const LightPtr& ptr) {
            if (existing_.find(ptr) == existing_.end()) return;
            existing_.erase(ptr);
            for (auto it = queue_.begin(); it != queue_.end(); ++it) {
                const LightPtr& light = *it;
                if (ptr == light) {
                    queue_.erase(it);
                    return;
                }
            }
        }

        // In case all lights need to be removed without being updated
        void Clear() {
            queue_.clear();
            existing_.clear();
        }

        size_t Size() const {
            return queue_.size();
        }

    private:
        std::deque<LightPtr> queue_;
        std::unordered_set<LightPtr> existing_;
    };

    // Represents data for current active frame
    struct RendererFrame {
        uint32_t viewportWidth;
        uint32_t viewportHeight;
        Radians fovy;
        CameraPtr camera;
        RendererMaterialInformation materialInfo;
        RendererCascadeContainer csc;
        std::vector<std::unordered_map<RenderFaceCulling, GpuCommandBufferPtr>> instancedDynamicPbrMeshes;
        std::vector<std::unordered_map<RenderFaceCulling, GpuCommandBufferPtr>> instancedStaticPbrMeshes;
        std::vector<std::unordered_map<RenderFaceCulling, GpuCommandBufferPtr>> instancedFlatMeshes;
        std::unordered_map<RenderFaceCulling, GpuCommandBufferPtr> visibleFirstLodInstancedDynamicPbrMeshes;
        std::unordered_map<RenderFaceCulling, GpuCommandBufferPtr> visibleFirstLodInstancedStaticPbrMeshes;
        std::unordered_map<RenderFaceCulling, GpuCommandBufferPtr> visibleFirstLodInstancedFlatMeshes;
        std::unordered_set<LightPtr> lights;
        std::unordered_set<LightPtr> virtualPointLights; // data is in lights
        LightUpdateQueue lightsToUpate; // shadow map data is invalid
        std::unordered_set<LightPtr> lightsToRemove;
        float znear;
        float zfar;
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 projectionView;
        glm::mat4 invProjectionView;
        glm::vec4 clearColor;
        TextureHandle skybox = TextureHandle::Null();
        glm::vec3 skyboxColorMask = glm::vec3(1.0f);
        float skyboxIntensity = 3.0f;
        glm::vec3 fogColor = glm::vec3(0.5f);
        float fogDensity = 0.0f;
        bool viewportDirty;
        bool vsyncEnabled;
        bool globalIlluminationEnabled = true;
    };

    class RendererBackend {
        // Geometry buffer
        struct GBuffer {
            FrameBuffer fbo;
            //Texture position;                 // RGB16F (rgba instead of rgb due to possible alignment issues)
            Texture normals;                  // RGB16F
            Texture albedo;                   // RGB16F
            Texture baseReflectivity;         // RGB16F
            Texture roughnessMetallicAmbient; // RGB16F
            Texture structure;                // RGBA16F
            Texture depth;                    // Default bit depth
        };

        struct PostFXBuffer {
            FrameBuffer fbo;
        };

        struct VirtualPointLightData {
            // For splitting viewport into tiles
            const int tileXDivisor = 5;
            const int tileYDivisor = 5;
            // This needs to match what is in the vpl tiled deferred shader compute header!
            int vplShadowCubeMapX = 64, vplShadowCubeMapY = 64;
            GpuBuffer vplDiffuseMaps;
            GpuBuffer vplShadowMaps;
            GpuBuffer vplStage1Results;
            GpuBuffer vplVisiblePerTile;
            GpuBuffer vplData;
            GpuBuffer vplVisibleIndices;
            GpuBuffer vplNumVisible;
            FrameBuffer vplGIFbo;
            Texture vplGIColorBuffer;
            FrameBuffer vplGIBlurredFbo;
            Texture vplGIBlurredBuffer;
        };

        struct RenderState {
            int numRegularShadowMaps = 80;
            int shadowCubeMapX = 256, shadowCubeMapY = 256;
            int maxShadowCastingLightsPerFrame = 20; // per frame
            int maxTotalRegularLightsPerFrame = 200; // per frame
            GpuBuffer nonShadowCastingPointLights;
            GpuBuffer shadowCubeMaps;
            GpuBuffer shadowCastingPointLights;
            VirtualPointLightData vpls;
            // How many shadow maps can be rebuilt each frame
            // Lights are inserted into a queue to prevent any light from being
            // over updated or neglected
            int maxShadowUpdatesPerFrame = 3;
            //std::shared_ptr<Camera> camera;
            Pipeline * currentShader = nullptr;
            // Buffer where all color data is written
            GBuffer buffer;
            // Buffer for lighting pass
            FrameBuffer lightingFbo;
            Texture lightingColorBuffer;
            // Used for effects like bloom
            Texture lightingHighBrightnessBuffer;
            Texture lightingDepthBuffer;
            // Used for Screen Space Ambient Occlusion (SSAO)
            Texture ssaoOffsetLookup;               // 4x4 table where each pixel is (16-bit, 16-bit)
            Texture ssaoOcclusionTexture;
            FrameBuffer ssaoOcclusionBuffer;        // Contains light factors computed per pixel
            Texture ssaoOcclusionBlurredTexture;
            FrameBuffer ssaoOcclusionBlurredBuffer; // Counteracts low sample count of occlusion buffer by depth-aware blurring
            // Used for atmospheric shadowing
            FrameBuffer atmosphericFbo;
            Texture atmosphericTexture;
            Texture atmosphericNoiseTexture;
            // Used for fast approximate anti-aliasing (FXAA)
            PostFXBuffer fxaaFbo1;
            PostFXBuffer fxaaFbo2;
            // Need to keep track of these to clear them at the end of each frame
            std::vector<GpuArrayBuffer> gpuBuffers;
            // For everything else including bloom post-processing
            int numBlurIterations = 10;
            // Might change from frame to frame
            int numDownsampleIterations = 0;
            int numUpsampleIterations = 0;
            std::vector<PostFXBuffer> gaussianBuffers;
            std::vector<PostFXBuffer> postFxBuffers;
            // Handles atmospheric post processing
            PostFXBuffer atmosphericPostFxBuffer;
            // End of the pipeline should write to this
            Texture finalScreenTexture;
            // Used for a call to glBlendFunc
            GLenum blendSFactor = GL_ONE;
            GLenum blendDFactor = GL_ZERO;
            // Skybox
            std::unique_ptr<Pipeline> skybox;
            // Postprocessing shader which allows for application
            // of hdr and gamma correction
            std::unique_ptr<Pipeline> hdrGamma;
            // Preprocessing shader which sets up the scene to allow for dynamic shadows
            std::vector<std::unique_ptr<Pipeline>> shadows;
            std::vector<std::unique_ptr<Pipeline>> vplShadows;
            // Geometry pass - handles all combinations of material properties
            std::unique_ptr<Pipeline> geometry;
            // Forward rendering pass
            std::unique_ptr<Pipeline> forward;
            // Handles first SSAO pass (no blurring)
            std::unique_ptr<Pipeline> ssaoOcclude;
            // Handles second SSAO pass (blurring)
            std::unique_ptr<Pipeline> ssaoBlur;
            // Handles the atmospheric shadowing stage
            std::unique_ptr<Pipeline> atmospheric;
            // Handles atmospheric post fx stage
            std::unique_ptr<Pipeline> atmosphericPostFx;
            // Handles the lighting stage
            std::unique_ptr<Pipeline> lighting;
            std::unique_ptr<Pipeline> lightingWithInfiniteLight;
            // Handles global illuminatino stage
            std::unique_ptr<Pipeline> vplGlobalIllumination;
            std::unique_ptr<Pipeline> vplGlobalIlluminationBlurring;
            // Bloom stage
            std::unique_ptr<Pipeline> bloom;
            // Handles virtual point light culling
            std::unique_ptr<Pipeline> vplCulling;
            std::unique_ptr<Pipeline> vplColoring;
            std::unique_ptr<Pipeline> vplTileDeferredCullingStage1;
            std::unique_ptr<Pipeline> vplTileDeferredCullingStage2;
            // Draws axis-aligned bounding boxes
            std::unique_ptr<Pipeline> aabbDraw;
            // Handles cascading shadow map depth buffer rendering
            // (we compile one depth shader per cascade - max 6)
            std::vector<std::unique_ptr<Pipeline>> csmDepth;
            std::vector<std::unique_ptr<Pipeline>> csmDepthRunAlphaTest;
            // Handles fxaa luminance followed by fxaa smoothing
            std::unique_ptr<Pipeline> fxaaLuminance;
            std::unique_ptr<Pipeline> fxaaSmoothing;
            std::vector<Pipeline *> shaders;
            // Generic unit cube to render as skybox
            EntityPtr skyboxCube;
            // Generic screen quad so we can render the screen
            // from a separate frame buffer
            EntityPtr screenQuad;
            // Gets around what might be a driver bug...
            TextureHandle dummyCubeMap;
        };

        struct TextureCache {
            std::string file;
            TextureHandle handle = TextureHandle::Null();
            Texture texture;
            /**
             * If true then the file is currently loaded into memory.
             * If false then it has been unloaded, so if anyone tries
             * to use it then it needs to first be re-loaded.
             */
            bool loaded = true;
        };

        struct ShadowMap3D {
            // Each shadow map is rendered to a frame buffer backed by a 3D texture
            FrameBuffer frameBuffer;
            Texture shadowCubeMap;
            // This is only set for VPLs so they can sample color values in the direction
            // of the world light
            Texture diffuseCubeMap;
        };

        struct ShadowMapCache {
            /**
             * Maps all shadow maps to a handle.
             */
            std::unordered_map<TextureHandle, ShadowMap3D> shadowMap3DHandles;

            // Lights -> Handles map
            std::unordered_map<LightPtr, TextureHandle> lightsToShadowMap;

            // Marks which maps are in use by an active light
            std::unordered_set<TextureHandle> usedShadowMaps;

            // Marks which lights are currently in the cache
            std::list<LightPtr> lruLightCache;
        };

        // Contains the cache for regular lights
        ShadowMapCache smapCache_;

        // Contains the cache for virtual point lights
        ShadowMapCache vplSmapCache_;

        /**
         * Contains information about various different settings
         * which will affect final rendering.
         */
        RenderState state_;

        /**
         * Contains all of the shaders that are used by the renderer.
         */
        std::vector<Pipeline *> shaders_;

        /**
         * This encodes the same information as the _textures map, except
         * that it can be indexed by a TextureHandle for fast lookup of
         * texture handles attached to Material objects.
         */
        //mutable std::unordered_map<TextureHandle, TextureCache> _textureHandles;

        // Current frame data used for drawing
        std::shared_ptr<RendererFrame> frame_;

        /**
         * If the renderer was setup properly then this will be marked
         * true.
         */
        bool isValid_ = false;

    public:
        explicit RendererBackend(const uint32_t width, const uint32_t height, const std::string&);
        ~RendererBackend();

        /**
         * @return true if the renderer initialized itself properly
         *      and false if any errors occurred
         */
        bool Valid() const;

        const Pipeline * GetCurrentShader() const;

        //void invalidateAllTextures();

        void RecompileShaders();

        /**
         * Attempts to load a model if not already loaded. Be sure to check
         * the returned model's isValid() function.
         */
        // Model loadModel(const std::string & file);

        /**
         * Sets the render mode to be either ORTHOGRAPHIC (2d)
         * or PERSPECTIVE (3d).
         */
        // void setRenderMode(RenderMode mode);

        /**
         * IMPORTANT! This sets up the renderer for a new frame.
         *
         * @param clearScreen if false then renderer will begin
         * drawing without clearing the screen
         */
        void Begin(const std::shared_ptr<RendererFrame>&, bool clearScreen);

        // Takes all state set during Begin and uses it to render the scene
        void RenderScene();

        /**
         * Finalizes the current scene and displays it.
         */
        void End();

        // Returns window events since the last time this was called
        // std::vector<SDL_Event> PollInputEvents();

        // // Returns the mouse status as of the most recent frame
        // RendererMouseState GetMouseState() const;

    private:
        void InitializeVplData_();
        void ClearGBuffer_();
        void UpdateWindowDimensions_();
        void ClearFramebufferData_(const bool);
        void InitPointShadowMaps_();
        // void _InitAllEntityMeshData();
        void InitCoreCSMData_(Pipeline *);
        void InitLights_(Pipeline * s, const std::vector<std::pair<LightPtr, double>> & lights, const size_t maxShadowLights);
        void InitSSAO_();
        void InitAtmosphericShadowing_();
        // void _InitEntityMeshData(RendererEntityData &);
        // void _ClearEntityMeshData();
        void ClearRemovedLightData_();
        void BindShader_(Pipeline *);
        void UnbindShader_();
        void PerformPostFxProcessing_();
        void PerformBloomPostFx_();
        void PerformAtmosphericPostFx_();
        void PerformFxaaPostFx_();
        void FinalizeFrame_();
        void InitializePostFxBuffers_();
        void RenderBoundingBoxes_(GpuCommandBufferPtr&);
        void RenderBoundingBoxes_(std::unordered_map<RenderFaceCulling, GpuCommandBufferPtr>&);
        void RenderImmediate_(const RenderFaceCulling, GpuCommandBufferPtr&);
        void Render_(const RenderFaceCulling, GpuCommandBufferPtr&, bool isLightInteracting, bool removeViewTranslation = false);
        void Render_(std::unordered_map<RenderFaceCulling, GpuCommandBufferPtr>&, bool isLightInteracting, bool removeViewTranslation = false);
        void InitVplFrameData_(const std::vector<std::pair<LightPtr, double>>& perVPLDistToViewer);
        void RenderImmediate_(std::unordered_map<RenderFaceCulling, GpuCommandBufferPtr>&);
        void UpdatePointLights_(std::vector<std::pair<LightPtr, double>>&, 
                                std::vector<std::pair<LightPtr, double>>&, 
                                std::vector<std::pair<LightPtr, double>>&,
                                std::vector<int>& visibleVplIndices);
        void PerformVirtualPointLightCullingStage1_(const std::vector<std::pair<LightPtr, double>>&, std::vector<int>& visibleVplIndices);
        void PerformVirtualPointLightCullingStage2_(const std::vector<std::pair<LightPtr, double>>&, const std::vector<int>& visibleVplIndices);
        void ComputeVirtualPointLightGlobalIllumination_(const std::vector<std::pair<LightPtr, double>>&);
        void RenderCSMDepth_();
        void RenderQuad_();
        void RenderSkybox_();
        void RenderSsaoOcclude_();
        void RenderSsaoBlur_();
        glm::vec3 CalculateAtmosphericLightPosition_() const;
        void RenderAtmosphericShadowing_();
        TextureHandle CreateShadowMap3D_(uint32_t resolutionX, uint32_t resolutionY, bool vpl);
        TextureHandle GetOrAllocateShadowMapHandleForLight_(LightPtr);
        ShadowMap3D GetOrAllocateShadowMapForLight_(LightPtr);
        void SetLightShadowMapHandle_(LightPtr, TextureHandle);
        void EvictLightFromShadowMapCache_(LightPtr);
        void AddLightToShadowMapCache_(LightPtr);
        void RemoveLightFromShadowMapCache_(LightPtr);
        bool ShadowMapExistsForLight_(LightPtr);
        ShadowMapCache& GetSmapCacheForLight_(LightPtr);
        Texture LookupShadowmapTexture_(TextureHandle handle) const;
        void RecalculateCascadeData_();
        void ValidateAllShaders_();
    };
}

#endif //STRATUSGFX_RENDERER_H
