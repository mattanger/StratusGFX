#pragma once

#include "GL/gl3w.h"
#include "glm/glm.hpp"
#include <memory>
#include <vector>
#include <forward_list>
#include <cstdint>
#include "StratusCommon.h"
#include "StratusGpuCommon.h"
#include <unordered_set>

namespace stratus {
    enum class GpuBindingPoint : int {
        // Good for things like vertices and normals
        ARRAY_BUFFER            = BITMASK64_POW2(1),
        // Good for indices
        ELEMENT_ARRAY_BUFFER    = BITMASK64_POW2(2),
        // Allows read-only uniform buffer access
        UNIFORM_BUFFER          = BITMASK64_POW2(3),
        // Allows read and write shader buffer access
        SHADER_STORAGE_BUFFER   = BITMASK64_POW2(4),
        // Allows for indirect array and element draw commands
        DRAW_INDIRECT_BUFFER    = BITMASK64_POW2(5)
    };

    // A more restrictive set of bindings good for things like floating point (vertex, normal, etc.)
    // and integer (index) buffers
    enum class GpuPrimitiveBindingPoint : int {
        ARRAY_BUFFER            = int(GpuBindingPoint::ARRAY_BUFFER),
        ELEMENT_ARRAY_BUFFER    = int(GpuBindingPoint::ELEMENT_ARRAY_BUFFER)
    };

    enum class GpuBaseBindingPoint : int {
        UNIFORM_BUFFER          = int(GpuBindingPoint::UNIFORM_BUFFER),
        SHADER_STORAGE_BUFFER   = int(GpuBindingPoint::SHADER_STORAGE_BUFFER)
    };

    enum class GpuStorageType : int {
        BYTE,
        UNSIGNED_BYTE,
        SHORT,
        UNSIGNED_SHORT,
        INT,
        UNSIGNED_INT,
        FLOAT
    };

    typedef int Bitfield;

    // Describes how the data will likely be used, meaning whether it will be changed
    // frequently, mapped for read/write or mapped persistently

    // Data will be set at creation and never touched again
    constexpr Bitfield GPU_STATIC_DATA = BITMASK64_POW2(0);
    // Data will be copied directly through the copy API often
    constexpr Bitfield GPU_DYNAMIC_DATA = BITMASK64_POW2(1);
    // Memory will be mapped for reading
    constexpr Bitfield GPU_MAP_READ = BITMASK64_POW2(2);
    // Memory will be mapped for writing outside of the API copy calls
    constexpr Bitfield GPU_MAP_WRITE = BITMASK64_POW2(3);
    // Memory will be mapped and continuously read from and written to without unmapping
    constexpr Bitfield GPU_MAP_PERSISTENT = BITMASK64_POW2(4);
    // Memory writes between client and server will be seen
    constexpr Bitfield GPU_MAP_COHERENT = BITMASK64_POW2(5);

    struct GpuBufferImpl;
    struct GpuArrayBufferImpl;
    struct GpuCommandBuffer;

    typedef std::shared_ptr<GpuCommandBuffer> GpuCommandBufferPtr;

    // A gpu buffer holds primitive data usually in the form of floats, ints and shorts
    // TODO: Look into use cases for things other than STATIC_DRAW
    struct GpuBuffer {
        GpuBuffer() {}
        GpuBuffer(const void * data, const uintptr_t sizeBytes, const Bitfield usage = GPU_MAP_READ | GPU_MAP_WRITE);
        virtual ~GpuBuffer() = default;

        void EnableAttribute(int32_t attribute, int32_t sizePerElem, GpuStorageType, bool normalized, uint32_t stride, uint32_t offset, uint32_t divisor = 0);
        virtual void Bind(const GpuBindingPoint) const;
        virtual void Unbind(const GpuBindingPoint) const;
        // From what I can tell there shouldn't be a need to unbind UBOs
        virtual void BindBase(const GpuBaseBindingPoint, const uint32_t index) const;

        // Maps the GPU memory into system memory - make sure READ, WRITE, or PERSISTENT mapping is enabled
        void * MapMemory(const Bitfield access = GPU_MAP_READ | GPU_MAP_WRITE) const;
        void UnmapMemory() const;
        bool IsMemoryMapped() const;

        uintptr_t SizeBytes() const;
        // Make sure GPU_DYNAMIC_DATA is set
        void CopyDataToBuffer(intptr_t offset, uintptr_t size, const void * data);
        void CopyDataFromBuffer(const GpuBuffer&);
        void CopyDataFromBufferToSysMem(intptr_t offset, uintptr_t size, void * data);

        // Memory mapping and data copying won't work after this
        void FinalizeMemory();

        bool operator==(const GpuBuffer& other) const {
            // Pointer comparison
            return this->impl_ == other.impl_;
        }

        bool operator!=(const GpuBuffer& other) const {
            return !(this->operator==(other));
        }

    protected:
        std::shared_ptr<GpuBufferImpl> impl_;
    };

    struct GpuPrimitiveBuffer final : public GpuBuffer {
        GpuPrimitiveBuffer() : GpuBuffer() {}
        GpuPrimitiveBuffer(const GpuPrimitiveBindingPoint type, const void * data, const uintptr_t sizeBytes, const Bitfield usage = 0);
        virtual ~GpuPrimitiveBuffer() = default;

        void Bind() const;
        void Unbind() const;

    private:
        GpuPrimitiveBindingPoint type_;
    };

    // Holds different gpu buffers and can bind/unbind them all as a group
    struct GpuArrayBuffer final {
        GpuArrayBuffer();
        ~GpuArrayBuffer() = default;

        void AddBuffer(const GpuPrimitiveBuffer&);
        size_t GetNumBuffers() const;
        GpuPrimitiveBuffer& GetBuffer(size_t);
        const GpuPrimitiveBuffer& GetBuffer(size_t) const;
        void UnmapAllMemory() const;
        bool IsMemoryMapped() const;
        void FinalizeAllMemory();
        void Bind() const;
        void Unbind() const;
        void Clear();

    private:
        std::shared_ptr<std::vector<std::unique_ptr<GpuPrimitiveBuffer>>> buffers_;
    };

    // Responsible for allocating vertex and index data. All data is stored
    // in two giant GPU buffers (one for vertices, one for indices).
    //
    // This is NOT thread safe as only the main thread should be using it since 
    // it performs GPU memory allocation.
    //
    // It can support a maximum of UINT_MAX vertices and UINT_MAX indices.
    class GpuMeshAllocator final {
        // This class initializes the global GPU memory for this class
        friend class GraphicsDriver;

        struct _MeshData {
            size_t nextByte;
            size_t lastByte;
        };

        GpuMeshAllocator() {}

    public:
        // Allocates 64-byte block vertex data where each element represents a GpuMeshData type.
        //
        // @return offset into global GPU vertex data array where data begins
        static uint32_t AllocateVertexData(const uint32_t numVertices);
        // @return offset into global GPU index data array where data begins
        static uint32_t AllocateIndexData(const uint32_t numIndices);

        // Deallocation
        static void DeallocateVertexData(const uint32_t offset, const uint32_t numVertices);
        static void DeallocateIndexData(const uint32_t offset, const uint32_t numIndices);

        static void CopyVertexData(const std::vector<GpuMeshData>&, const uint32_t offset);
        static void CopyIndexData(const std::vector<uint32_t>&, const uint32_t offset);

        // Binds the GpuMesh buffer
        static void BindBase(const GpuBaseBindingPoint&, const uint32_t);
        // Binds/unbinds indices buffer
        static void BindElementArrayBuffer();
        static void UnbindElementArrayBuffer();

        static uint32_t FreeVertices();
        static uint32_t FreeIndices();

    private:
        static _MeshData * FindFreeSlot_(std::vector<_MeshData>&, const size_t bytes);
        static uint32_t AllocateData_(const uint32_t size, const size_t byteMultiplier, const size_t maxBytes, 
                                      GpuBuffer&, _MeshData&, std::vector<GpuMeshAllocator::_MeshData>&);
        static void DeallocateData_(_MeshData&, std::vector<GpuMeshAllocator::_MeshData>&, const size_t offsetBytes, const size_t lastByte);
        static void Initialize_();
        static void Shutdown_();
        static void Resize_(GpuBuffer& buffer, _MeshData& data, const size_t newSizeBytes);
        static size_t RemainingBytes_(const _MeshData& data);

    private:
        static GpuBuffer vertices_;
        static GpuBuffer indices_;
        // Allows for O(1) allocation when data is available
        static _MeshData lastVertex_;
        static _MeshData lastIndex_;
        // Allows for O(N) allocation by searching for previously deallocated
        // chunks of memory
        static std::vector<_MeshData> freeVertices_;
        static std::vector<_MeshData> freeIndices_;
        static bool initialized_;
    };

    // Stores material indices, model transforms and indirect draw commands
    class GpuCommandBuffer final {
        GpuBuffer materialIndices_;
        GpuBuffer globalTransforms_;
        GpuBuffer modelTransforms_;
        GpuBuffer indirectDrawCommands_;
        GpuBuffer aabbs_;

    public:
        GpuCommandBuffer();

        // This is to allow for 64-bit handles to be used to identify an object
        // with its location in the array
        // std::unordered_map<uint64_t, size_t> handlesToIndicesMap;
        // std::vector<uint64_t> handles;
        // CPU side of the data
        std::vector<uint32_t> materialIndices;
        // Sometimes (for example with AABBs) we only want the global transform which excludes
        // the mesh transform
        std::vector<glm::mat4> globalTransforms;
        // Model transform is defined as global transform * mesh transform
        std::vector<glm::mat4> modelTransforms;
        std::vector<GpuDrawElementsIndirectCommand> indirectDrawCommands;
        std::vector<GpuAABB> aabbs;

        GpuCommandBuffer(GpuCommandBuffer&&) = default;
        GpuCommandBuffer(const GpuCommandBuffer&) = delete;

        GpuCommandBuffer& operator=(GpuCommandBuffer&&) = delete;
        GpuCommandBuffer& operator=(const GpuCommandBuffer&) = delete;

        void RemoveCommandsAt(const std::unordered_set<size_t>& indices);
        size_t NumDrawCommands() const;
        void UploadDataToGpu();

        void BindMaterialIndicesBuffer(uint32_t index);
        void BindGlobalTransformBuffer(uint32_t index);
        void BindModelTransformBuffer(uint32_t index);
        void BindAabbBuffer(uint32_t index);

        void BindIndirectDrawCommands();
        void UnbindIndirectDrawCommands();

        const GpuBuffer& GetIndirectDrawCommandsBuffer() const;

    private:
        void VerifyArraySizes_() const;
    };
}