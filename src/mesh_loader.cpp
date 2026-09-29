#include "mesh_loader.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace meshviewer
{
namespace
{

constexpr unsigned int kPostProcess = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenNormals |
                                      aiProcess_ImproveCacheLocality | aiProcess_SortByPType;

std::string Utf8Path(const std::filesystem::path &path)
{
    const auto utf8 = path.u8string();
    return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
}

std::filesystem::path PathFromUtf8(std::string_view text)
{
    const std::u8string utf8(reinterpret_cast<const char8_t *>(text.data()), text.size());
    return std::filesystem::path(utf8);
}

bool MaterialTexturePath(const aiMaterial *material, aiTextureType type, aiString &path)
{
    return material != nullptr && aiGetMaterialTexture(material, type, 0, &path) == AI_SUCCESS && path.length > 0;
}

bool FindBaseColorTexture(const aiMaterial *material, aiString &path)
{
    return MaterialTexturePath(material, aiTextureType_BASE_COLOR, path) ||
           MaterialTexturePath(material, aiTextureType_DIFFUSE, path);
}

void StoreEmbeddedTexture(const aiTexture *embedded, CpuMesh &mesh)
{
    if (embedded->pcData == nullptr || embedded->mWidth == 0)
    {
        throw std::runtime_error("embedded texture is empty");
    }
    if (embedded->mHeight == 0)
    {
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(embedded->pcData);
        mesh.textureEncoded.assign(bytes, bytes + embedded->mWidth);
        return;
    }

    const std::uint32_t width = embedded->mWidth;
    const std::uint32_t height = embedded->mHeight;
    mesh.textureWidth = width;
    mesh.textureHeight = height;
    mesh.textureRgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::uint32_t index = 0; index < width * height; ++index)
    {
        const aiTexel &texel = embedded->pcData[index];
        std::uint8_t *pixel = mesh.textureRgba.data() + static_cast<std::size_t>(index) * 4;
        pixel[0] = texel.r;
        pixel[1] = texel.g;
        pixel[2] = texel.b;
        pixel[3] = texel.a;
    }
}

std::filesystem::path FindTextureFile(const std::filesystem::path &meshDirectory, const aiString &texturePath)
{
    const std::filesystem::path asGiven = PathFromUtf8(std::string_view(texturePath.data, texturePath.length));
    if (asGiven.is_absolute() && std::filesystem::is_regular_file(asGiven))
    {
        return asGiven;
    }
    if (!meshDirectory.empty())
    {
        const std::filesystem::path besideMesh = meshDirectory / asGiven;
        if (std::filesystem::is_regular_file(besideMesh))
        {
            return besideMesh;
        }
        const std::filesystem::path byName = meshDirectory / asGiven.filename();
        if (std::filesystem::is_regular_file(byName))
        {
            return byName;
        }
    }
    if (std::filesystem::is_regular_file(asGiven))
    {
        return asGiven;
    }
    throw std::runtime_error(std::string("texture file not found: ") + texturePath.C_Str());
}

void AttachBaseColorTexture(const aiScene *scene, const std::filesystem::path &meshDirectory, CpuMesh &mesh)
{
    for (unsigned int index = 0; index < scene->mNumMaterials; ++index)
    {
        aiString texturePath;
        if (!FindBaseColorTexture(scene->mMaterials[index], texturePath))
        {
            continue;
        }

        const char *name = texturePath.C_Str();
        if (const aiTexture *embedded = scene->GetEmbeddedTexture(name))
        {
            StoreEmbeddedTexture(embedded, mesh);
            return;
        }
        mesh.textureFile = FindTextureFile(meshDirectory, texturePath);
        return;
    }
}

void ExpandBounds(CpuMesh &mesh, float x, float y, float z)
{
    mesh.boundsMin[0] = std::min(mesh.boundsMin[0], x);
    mesh.boundsMin[1] = std::min(mesh.boundsMin[1], y);
    mesh.boundsMin[2] = std::min(mesh.boundsMin[2], z);
    mesh.boundsMax[0] = std::max(mesh.boundsMax[0], x);
    mesh.boundsMax[1] = std::max(mesh.boundsMax[1], y);
    mesh.boundsMax[2] = std::max(mesh.boundsMax[2], z);
}

aiMatrix3x3 NormalMatrix(const aiMatrix4x4 &transform)
{
    aiMatrix3x3 linear(transform);
    if (std::fabs(linear.Determinant()) > 1.0e-8f)
    {
        linear.Inverse();
        linear.Transpose();
    }
    return linear;
}

void AppendMesh(const aiMesh *source, const aiMatrix4x4 &transform, CpuMesh &mesh)
{
    if (source->mPrimitiveTypes != 0 && (source->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0)
    {
        return;
    }
    if (source->mNumVertices == 0 || source->mNumFaces == 0)
    {
        return;
    }
    if (!source->mNormals)
    {
        throw std::runtime_error("Assimp mesh is missing normals after import");
    }

    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    if (base > std::numeric_limits<std::uint32_t>::max() - source->mNumVertices)
    {
        throw std::runtime_error("mesh is too large for a 32-bit index buffer");
    }

    const aiMatrix3x3 normals = NormalMatrix(transform);
    mesh.vertices.reserve(mesh.vertices.size() + source->mNumVertices);
    for (unsigned int index = 0; index < source->mNumVertices; ++index)
    {
        const aiVector3D position = transform * source->mVertices[index];
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        {
            throw std::runtime_error("mesh contains a non-finite position");
        }
        aiVector3D normal = normals * source->mNormals[index];
        const float lengthSquared = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
        if (lengthSquared > 1.0e-12f)
        {
            const float inverse = 1.0f / std::sqrt(lengthSquared);
            normal.x *= inverse;
            normal.y *= inverse;
            normal.z *= inverse;
        }
        else
        {
            normal = aiVector3D(0.0f, 1.0f, 0.0f);
        }

        CpuVertex vertex{};
        vertex.position[0] = position.x;
        vertex.position[1] = position.y;
        vertex.position[2] = position.z;
        vertex.normal[0] = normal.x;
        vertex.normal[1] = normal.y;
        vertex.normal[2] = normal.z;
        if (source->HasTextureCoords(0))
        {
            const aiVector3D &uv = source->mTextureCoords[0][index];
            vertex.uv[0] = uv.x;
            vertex.uv[1] = uv.y;
        }
        else
        {
            vertex.uv[0] = 0.0f;
            vertex.uv[1] = 0.0f;
        }
        if (source->HasVertexColors(0))
        {
            const aiColor4D &color = source->mColors[0][index];
            vertex.color[0] = color.r;
            vertex.color[1] = color.g;
            vertex.color[2] = color.b;
            vertex.color[3] = color.a;
        }
        else
        {
            vertex.color[0] = 1.0f;
            vertex.color[1] = 1.0f;
            vertex.color[2] = 1.0f;
            vertex.color[3] = 1.0f;
        }
        mesh.vertices.push_back(vertex);
        ExpandBounds(mesh, position.x, position.y, position.z);
    }

    for (unsigned int faceIndex = 0; faceIndex < source->mNumFaces; ++faceIndex)
    {
        const aiFace &face = source->mFaces[faceIndex];
        if (face.mNumIndices != 3)
        {
            continue;
        }
        for (unsigned int corner = 0; corner < 3; ++corner)
        {
            const unsigned int local = face.mIndices[corner];
            if (local >= source->mNumVertices)
            {
                throw std::runtime_error("mesh face index is out of range");
            }
            mesh.indices.push_back(base + local);
        }
    }
}

void AppendNode(const aiScene *scene, const aiNode *node, const aiMatrix4x4 &parent, CpuMesh &mesh)
{
    const aiMatrix4x4 global = parent * node->mTransformation;
    for (unsigned int index = 0; index < node->mNumMeshes; ++index)
    {
        const unsigned int meshIndex = node->mMeshes[index];
        if (meshIndex < scene->mNumMeshes)
        {
            AppendMesh(scene->mMeshes[meshIndex], global, mesh);
        }
    }
    for (unsigned int child = 0; child < node->mNumChildren; ++child)
    {
        AppendNode(scene, node->mChildren[child], global, mesh);
    }
}

CpuMesh FromScene(const aiScene *scene, Assimp::Importer &importer, const std::filesystem::path &meshDirectory)
{
    if (!scene || !scene->mRootNode)
    {
        const char *error = importer.GetErrorString();
        throw std::runtime_error(std::string("Assimp failed to load mesh: ") +
                                 (error && error[0] != '\0' ? error : "unknown error"));
    }

    CpuMesh mesh;
    const float infinity = std::numeric_limits<float>::infinity();
    mesh.boundsMin[0] = mesh.boundsMin[1] = mesh.boundsMin[2] = infinity;
    mesh.boundsMax[0] = mesh.boundsMax[1] = mesh.boundsMax[2] = -infinity;
    AppendNode(scene, scene->mRootNode, aiMatrix4x4{}, mesh);

    if (mesh.vertices.empty() || mesh.indices.empty())
    {
        throw std::runtime_error("mesh file did not contain any triangles");
    }
    AttachBaseColorTexture(scene, meshDirectory, mesh);
    return mesh;
}

} // namespace

CpuMesh LoadMesh(const std::filesystem::path &path)
{
    if (path.empty())
    {
        throw std::runtime_error("mesh path is empty");
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
        throw std::runtime_error("mesh file not found: " + Utf8Path(path));
    }

    Assimp::Importer importer;
    const std::string utf8 = Utf8Path(path);
    const aiScene *scene = importer.ReadFile(utf8, kPostProcess);
    return FromScene(scene, importer, path.parent_path());
}

CpuMesh LoadMeshFromMemory(const void *data, std::size_t size, const char *hint)
{
    if (!data || size == 0)
    {
        throw std::runtime_error("mesh data is empty");
    }
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFileFromMemory(data, size, kPostProcess, hint);
    return FromScene(scene, importer, {});
}

MeshFit FitMesh(const CpuMesh &mesh)
{
    const float dx = mesh.boundsMax[0] - mesh.boundsMin[0];
    const float dy = mesh.boundsMax[1] - mesh.boundsMin[1];
    const float dz = mesh.boundsMax[2] - mesh.boundsMin[2];
    const float extent = std::max(dx, std::max(dy, dz));
    if (!(extent > 0.0f) || !std::isfinite(extent))
    {
        throw std::runtime_error("mesh has no spatial extent");
    }

    MeshFit fit;
    fit.scale = 2.0f / extent;
    for (int axis = 0; axis < 3; ++axis)
    {
        fit.center[axis] = 0.5f * (mesh.boundsMin[axis] + mesh.boundsMax[axis]);
    }
    return fit;
}

} // namespace meshviewer
