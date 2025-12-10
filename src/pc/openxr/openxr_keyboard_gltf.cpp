#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE  // We don't need image writing
#define TINYGLTF_NO_EXTERNAL_IMAGE   // Images are embedded in GLB, not external files

#include "../../include/GL/tiny_gltf.h"
#include "openxr_keyboard_gltf.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace KeyboardGLTF {

bool ParseTextureURI(const std::string& uri, uint64_t& textureId, uint32_t& width, uint32_t& height) {
    // URI format: metaVirtualKeyboard://texture/{textureID}?w={width}&h={height}&fmt=RGBA32
    
    if (uri.empty()) {
        return false;
    }
    
    if (uri.find("metaVirtualKeyboard://texture/") != 0) {
        return false;
    }
    
    size_t idStart = uri.find("/texture/") + 9;
    size_t idEnd = uri.find('?', idStart);
    if (idEnd == std::string::npos) return false;
    
    std::string idStr = uri.substr(idStart, idEnd - idStart);
    try {
        textureId = std::stoull(idStr);
    } catch (...) { return false; }
    
    size_t wStart = uri.find("w=", idEnd);
    if (wStart == std::string::npos) return false;
    wStart += 2;
    size_t wEnd = uri.find('&', wStart);
    if (wEnd == std::string::npos) return false;
    
    std::string wStr = uri.substr(wStart, wEnd - wStart);
    try {
        width = std::stoul(wStr);
    } catch (...) { return false; }
    
    size_t hStart = uri.find("h=", wEnd);
    if (hStart == std::string::npos) return false;
    hStart += 2;
    size_t hEnd = uri.find('&', hStart);
    if (hEnd == std::string::npos) hEnd = uri.length(); // might be last param
    
    std::string hStr = uri.substr(hStart, hEnd - hStart);
    try {
        height = std::stoul(hStr);
    } catch (...) { return false; }
    
    return true;
}

void MatrixIdentity(float* m) {
    memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void MatrixMultiply(const float* a, const float* b, float* out) {
    float res[16];
    // Column-major matrix multiplication: C = A * B
    // C[col][row] = sum(A[col][k] * B[k][row])
    // In linear indexing: C[col*4 + row] = sum(A[k*4 + row] * B[col*4 + k])
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            res[col * 4 + row] = sum;
        }
    }
    memcpy(out, res, sizeof(float) * 16);
}

void MatrixFromTRS(const std::vector<double>& t, const std::vector<double>& r, const std::vector<double>& s, float* out) {
    // Translation
    float tx = t.size() == 3 ? (float)t[0] : 0.0f;
    float ty = t.size() == 3 ? (float)t[1] : 0.0f;
    float tz = t.size() == 3 ? (float)t[2] : 0.0f;

    // Rotation (Quaternion)
    float qx = r.size() == 4 ? (float)r[0] : 0.0f;
    float qy = r.size() == 4 ? (float)r[1] : 0.0f;
    float qz = r.size() == 4 ? (float)r[2] : 0.0f;
    float qw = r.size() == 4 ? (float)r[3] : 1.0f;

    // Scale
    float sx = s.size() == 3 ? (float)s[0] : 1.0f;
    float sy = s.size() == 3 ? (float)s[1] : 1.0f;
    float sz = s.size() == 3 ? (float)s[2] : 1.0f;

    // Quaternion to Matrix
    float xx = qx * qx;
    float yy = qy * qy;
    float zz = qz * qz;
    float xy = qx * qy;
    float xz = qx * qz;
    float yz = qy * qz;
    float wx = qw * qx;
    float wy = qw * qy;
    float wz = qw * qz;

    float rm[16];
    MatrixIdentity(rm);
    rm[0] = 1.0f - 2.0f * (yy + zz);
    rm[1] = 2.0f * (xy + wz);
    rm[2] = 2.0f * (xz - wy);
    
    rm[4] = 2.0f * (xy - wz);
    rm[5] = 1.0f - 2.0f * (xx + zz);
    rm[6] = 2.0f * (yz + wx);
    
    rm[8] = 2.0f * (xz + wy);
    rm[9] = 2.0f * (yz - wx);
    rm[10] = 1.0f - 2.0f * (xx + yy);

    // Apply scale
    rm[0] *= sx; rm[1] *= sx; rm[2] *= sx;
    rm[4] *= sy; rm[5] *= sy; rm[6] *= sy;
    rm[8] *= sz; rm[9] *= sz; rm[10] *= sz;

    // Apply translation
    rm[12] = tx;
    rm[13] = ty;
    rm[14] = tz;

    memcpy(out, rm, sizeof(float) * 16);
}

void ProcessMesh(const tinygltf::Model& model, const tinygltf::Mesh& gltfMesh, const float* transform, int nodeIndex, Model& outModel) {
    // Create a new KeyboardModel for this mesh (Meta's pattern)
    // All primitives in this glTF mesh become surfaces in one KeyboardModel
    KeyboardModel keyboardModel;
    keyboardModel.name = gltfMesh.name;
    
    // Initialize weights from glTF mesh (shared by all surfaces)
    if (!gltfMesh.weights.empty()) {
        keyboardModel.weights.resize(gltfMesh.weights.size());
        for (size_t i = 0; i < gltfMesh.weights.size(); i++) {
            keyboardModel.weights[i] = (float)gltfMesh.weights[i];
        }
    }
    
    // Process each primitive as a separate ModelSurface
    for (size_t primIdx = 0; primIdx < gltfMesh.primitives.size(); primIdx++) {
        const auto& primitive = gltfMesh.primitives[primIdx];
        
        // Create ModelSurface for this primitive
        ModelSurface surface;
        
        // Get position accessor
        auto posIt = primitive.attributes.find("POSITION");
        auto texCoordIt = primitive.attributes.find("TEXCOORD_0");
        
        if (posIt == primitive.attributes.end()) {
            std::cerr << "glTF: Skipping primitive " << primIdx << " of mesh '" << gltfMesh.name << "' - missing POSITION attribute" << std::endl;
            continue;
        }
        
        if (texCoordIt == primitive.attributes.end()) {
            std::cerr << "glTF: Skipping primitive " << primIdx << " of mesh '" << gltfMesh.name << "' - missing TEXCOORD_0 attribute" << std::endl;
            continue;
        }
        
        const tinygltf::Accessor& posAccessor = model.accessors[posIt->second];
        const tinygltf::Accessor& texCoordAccessor = model.accessors[texCoordIt->second];
        const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
        
        // Get buffer views
        const tinygltf::BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
        const tinygltf::BufferView& texCoordBufferView = model.bufferViews[texCoordAccessor.bufferView];
        const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
        
        // Get buffers
        const tinygltf::Buffer& posBuffer = model.buffers[posBufferView.buffer];
        const tinygltf::Buffer& texCoordBuffer = model.buffers[texCoordBufferView.buffer];
        const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];
        
        // Get material and texture
        if (primitive.material >= 0 && primitive.material < model.materials.size()) {
            const auto& material = model.materials[primitive.material];
            
            // Get base color factor
            if (material.pbrMetallicRoughness.baseColorFactor.size() == 4) {
                surface.baseColor.x = (float)material.pbrMetallicRoughness.baseColorFactor[0];
                surface.baseColor.y = (float)material.pbrMetallicRoughness.baseColorFactor[1];
                surface.baseColor.z = (float)material.pbrMetallicRoughness.baseColorFactor[2];
                surface.baseColor.w = (float)material.pbrMetallicRoughness.baseColorFactor[3];
            }
            
            if (material.pbrMetallicRoughness.baseColorTexture.index >= 0) {
                int texIdx = material.pbrMetallicRoughness.baseColorTexture.index;
                
                if (texIdx < model.textures.size()) {
                    const auto& texture = model.textures[texIdx];
                    
                    if (texture.source >= 0 && texture.source < model.images.size()) {
                        const auto& image = model.images[texture.source];
                        
                        uint64_t tid;
                        uint32_t w, h;
                        bool hasUri = ParseTextureURI(image.uri, tid, w, h);
                        
                        if (hasUri) {
                            surface.textureId = tid;
                            
                            // Add to texture list if new
                            bool found = false;
                            for (const auto& t : outModel.textures) {
                                if (t.id == tid) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                TextureInfo t;
                                t.id = tid;
                                t.width = w;
                                t.height = h;
                                outModel.textures.push_back(t);
                            }
                        } else if (!image.image.empty()) {
                            tid = 0x8000000000000000 | (uint64_t)texture.source;
                            surface.textureId = tid;
                            
                            bool found = false;
                            for (const auto& t : outModel.textures) {
                                if (t.id == tid) {
                                    found = true;
                                    break;
                                }
                            }
                            
                            if (!found) {
                                TextureInfo t;
                                t.id = tid;
                                t.width = image.width;
                                t.height = image.height;
                                t.data = image.image;
                                outModel.textures.push_back(t);
                            }
                        }
                    }
                }
            }
        }
        
        // Extract vertices
        size_t vertexCount = posAccessor.count;
        surface.vertices.resize(vertexCount);
        
        const uint8_t* posData = posBuffer.data.data() + posBufferView.byteOffset + posAccessor.byteOffset;
        const uint8_t* texCoordData = texCoordBuffer.data.data() + texCoordBufferView.byteOffset + texCoordAccessor.byteOffset;
        
        size_t posStride = posBufferView.byteStride ? posBufferView.byteStride : sizeof(float) * 3;
        size_t texCoordStride = texCoordBufferView.byteStride ? texCoordBufferView.byteStride : sizeof(float) * 2;
        
        for (size_t i = 0; i < vertexCount; i++) {
            float pos[3];
            float texCoord[2];
            
            memcpy(pos, posData + i * posStride, sizeof(float) * 3);
            memcpy(texCoord, texCoordData + i * texCoordStride, sizeof(float) * 2);
            
            surface.vertices[i].position = {pos[0], pos[1], pos[2]};
            surface.vertices[i].texCoord = {texCoord[0], texCoord[1]};
        }
        
        // Extract indices
        size_t indexCount = indexAccessor.count;
        surface.indices.resize(indexCount);
        
        const uint8_t* indexData = indexBuffer.data.data() + indexBufferView.byteOffset + indexAccessor.byteOffset;
        
        for (size_t i = 0; i < indexCount; i++) {
            if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                uint16_t idx;
                memcpy(&idx, indexData + i * sizeof(uint16_t), sizeof(uint16_t));
                surface.indices[i] = idx;
            } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                uint32_t idx;
                memcpy(&idx, indexData + i * sizeof(uint32_t), sizeof(uint32_t));
                surface.indices[i] = static_cast<uint16_t>(idx);
            } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                surface.indices[i] = indexData[i];
            }
        }
        
       // Read morph targets
        for (const auto& target : primitive.targets) {
            MorphTarget morphTarget;
            morphTarget.position.resize(vertexCount, {0,0,0});
            morphTarget.normal.resize(vertexCount, {0,0,0});
            morphTarget.texCoord.resize(vertexCount, {0,0});
            
            auto posIt = target.find("POSITION");
            if (posIt != target.end()) {
                const tinygltf::Accessor& accessor = model.accessors[posIt->second];
                const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
                const uint8_t* data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
                size_t stride = bufferView.byteStride ? bufferView.byteStride : sizeof(float) * 3;
                
                for (size_t i = 0; i < vertexCount; i++) {
                    float val[3];
                    memcpy(val, data + i * stride, sizeof(float) * 3);
                    morphTarget.position[i] = {val[0], val[1], val[2]};
                }
            }
            
            auto normIt = target.find("NORMAL");
            if (normIt != target.end()) {
                const tinygltf::Accessor& accessor = model.accessors[normIt->second];
                const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
                const uint8_t* data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
                size_t stride = bufferView.byteStride ? bufferView.byteStride : sizeof(float) * 3;
                
                for (size_t i = 0; i < vertexCount; i++) {
                    float val[3];
                    memcpy(val, data + i * stride, sizeof(float) * 3);
                    morphTarget.normal[i] = {val[0], val[1], val[2]};
                }
            }
            
            auto texCoordIt = target.find("TEXCOORD_0");
            if (texCoordIt != target.end()) {
                const tinygltf::Accessor& accessor = model.accessors[texCoordIt->second];
                const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
                const uint8_t* data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
                size_t stride = bufferView.byteStride ? bufferView.byteStride : sizeof(float) * 2;
                
                for (size_t i = 0; i < vertexCount; i++) {
                    float val[2];
                    memcpy(val, data + i * stride, sizeof(float) * 2);
                    morphTarget.texCoord[i] = {val[0], val[1]};
                }
            }
            
            surface.morphTargets.push_back(morphTarget);
        }
        
        // Initialize weights if needed
        if (!primitive.targets.empty() && keyboardModel.weights.empty()) {
            keyboardModel.weights.resize(primitive.targets.size(), 0.0f);
            if (!gltfMesh.weights.empty()) {
                for (size_t i = 0; i < gltfMesh.weights.size() && i < keyboardModel.weights.size(); i++) {
                    keyboardModel.weights[i] = (float)gltfMesh.weights[i];
                }
            }
        }
        
        // Store original vertices for CPU morphing
        surface.originalVertices = surface.vertices;
        
        // Add surface to model
        keyboardModel.surfaces.push_back(surface);
    }
    
    // Add the KeyboardModel to the output
    outModel.models.push_back(keyboardModel);
}

void ParseAnimation(const tinygltf::Model& model, Model& outModel) {
    for (const auto& gltfAnim : model.animations) {
        Animation anim;
        anim.name = gltfAnim.name;
        
        // Parse samplers
        for (const auto& gltfSampler : gltfAnim.samplers) {
            AnimationSampler sampler;
            
            // Input (Time)
            {
                const tinygltf::Accessor& accessor = model.accessors[gltfSampler.input];
                const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
                const uint8_t* data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
                size_t stride = bufferView.byteStride ? bufferView.byteStride : sizeof(float);
                
                sampler.input.resize(accessor.count);
                for (size_t i = 0; i < accessor.count; i++) {
                    float val;
                    memcpy(&val, data + i * stride, sizeof(float));
                    sampler.input[i] = val;
                }
            }
            
            // Output (Values)
            {
                const tinygltf::Accessor& accessor = model.accessors[gltfSampler.output];
                const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
                const uint8_t* data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
                
                // Calculate proper stride based on accessor type
                // Accessor type can be SCALAR (1), VEC2 (2), VEC3 (3), VEC4 (4), etc.
                int numComponents = 1; // Default to SCALAR
                if (accessor.type == TINYGLTF_TYPE_SCALAR) numComponents = 1;
                else if (accessor.type == TINYGLTF_TYPE_VEC2) numComponents = 2;
                else if (accessor.type == TINYGLTF_TYPE_VEC3) numComponents = 3;
                else if (accessor.type == TINYGLTF_TYPE_VEC4) numComponents = 4;
                else if (accessor.type == TINYGLTF_TYPE_MAT2) numComponents = 4;
                else if (accessor.type == TINYGLTF_TYPE_MAT3) numComponents = 9;
                else if (accessor.type == TINYGLTF_TYPE_MAT4) numComponents = 16;
                
                // For animation output, each "count" represents one keyframe
                // and each keyframe might have multiple components
                size_t componentSize = sizeof(float); // assuming GL_FLOAT
                size_t stride = bufferView.byteStride ? bufferView.byteStride : (componentSize * numComponents);
                
                // Total number of float values = count * numComponents
                size_t totalFloats = accessor.count * numComponents;
                sampler.output.resize(totalFloats);
                
                for (size_t i = 0; i < accessor.count; i++) {
                    const uint8_t* elementData = data + i * stride;
                    for (int c = 0; c < numComponents; c++) {
                        float val;
                        memcpy(&val, elementData + c * componentSize, sizeof(float));
                        sampler.output[i * numComponents + c] = val;
                    }
                }
            }
            
            anim.samplers.push_back(sampler);
        }
        
        // Parse channels
        for (const auto& gltfChannel : gltfAnim.channels) {
            AnimationChannel channel;
            channel.samplerIndex = gltfChannel.sampler;
            channel.nodeIndex = gltfChannel.target_node;
            channel.path = gltfChannel.target_path;
            
            if (gltfChannel.extras.Has("additiveWeightIndex")) {
                const tinygltf::Value& value = gltfChannel.extras.Get("additiveWeightIndex");
                if (value.IsInt()) {
                    channel.additiveWeightIndex = value.Get<int>();
                }
            }
            
            anim.channels.push_back(channel);
        }
        
        outModel.animations.push_back(anim);
    }
}

void ProcessNode(const tinygltf::Model& model, int nodeIdx, int parentIdx, const float* parentTransform, Model& outModel) {
    if (nodeIdx < 0 || nodeIdx >= model.nodes.size()) return;
    const auto& node = model.nodes[nodeIdx];
    
    float localTransform[16];
    MatrixIdentity(localTransform);

    // Store TRS values (either from direct TRS or decomposed from matrix)
    // Use defaults if not specified
    std::vector<double> translation = node.translation.size() == 3 ? node.translation : std::vector<double>{0.0, 0.0, 0.0};
    std::vector<double> rotation = node.rotation.size() == 4 ? node.rotation : std::vector<double>{0.0, 0.0, 0.0, 1.0};
    std::vector<double> scale = node.scale.size() == 3 ? node.scale : std::vector<double>{1.0, 1.0, 1.0};

    if (node.matrix.size() == 16) {
        // Matrix is provided - need to decompose into TRS like Meta does
        // First, copy matrix to local transform (glTF uses column-major)
        for (int i = 0; i < 16; ++i) localTransform[i] = (float)node.matrix[i];
        
        // TRANSLATION - Extract from last column
        translation = {node.matrix[12], node.matrix[13], node.matrix[14]};
        
        // SCALE - Extract magnitude of each basis vector
        float scaleX = sqrtf(
            localTransform[0] * localTransform[0] +
            localTransform[1] * localTransform[1] +
            localTransform[2] * localTransform[2]);
        float scaleY = sqrtf(
            localTransform[4] * localTransform[4] +
            localTransform[5] * localTransform[5] +
            localTransform[6] * localTransform[6]);
        float scaleZ = sqrtf(
            localTransform[8] * localTransform[8] +
            localTransform[9] * localTransform[9] +
            localTransform[10] * localTransform[10]);
        
        scale = {scaleX, scaleY, scaleZ};
        
        // ROTATION - Extract quaternion from normalized rotation matrix
        // Normalize the rotation part by removing scale
        const float rcpScaleX = 1.0f / scaleX;
        const float rcpScaleY = 1.0f / scaleY;
        const float rcpScaleZ = 1.0f / scaleZ;
        
        const float m[9] = {
            localTransform[0] * rcpScaleX,
            localTransform[1] * rcpScaleX,
            localTransform[2] * rcpScaleX,
            localTransform[4] * rcpScaleY,
            localTransform[5] * rcpScaleY,
            localTransform[6] * rcpScaleY,
            localTransform[8] * rcpScaleZ,
            localTransform[9] * rcpScaleZ,
            localTransform[10] * rcpScaleZ
        };
        
        // Convert rotation matrix to quaternion (Meta's algorithm)
        float qx, qy, qz, qw;
        if (m[0 * 3 + 0] + m[1 * 3 + 1] + m[2 * 3 + 2] > 0.0f) {
            float t = +m[0 * 3 + 0] + m[1 * 3 + 1] + m[2 * 3 + 2] + 1.0f;
            float s = 1.0f / sqrtf(t) * 0.5f; // Using 1/sqrt instead of RcpSqrt
            qw = s * t;
            qz = (m[0 * 3 + 1] - m[1 * 3 + 0]) * s;
            qy = (m[2 * 3 + 0] - m[0 * 3 + 2]) * s;
            qx = (m[1 * 3 + 2] - m[2 * 3 + 1]) * s;
        } else if (m[0 * 3 + 0] > m[1 * 3 + 1] && m[0 * 3 + 0] > m[2 * 3 + 2]) {
            float t = +m[0 * 3 + 0] - m[1 * 3 + 1] - m[2 * 3 + 2] + 1.0f;
            float s = 1.0f / sqrtf(t) * 0.5f;
            qx = s * t;
            qy = (m[0 * 3 + 1] + m[1 * 3 + 0]) * s;
            qz = (m[2 * 3 + 0] + m[0 * 3 + 2]) * s;
            qw = (m[1 * 3 + 2] - m[2 * 3 + 1]) * s;
        } else if (m[1 * 3 + 1] > m[2 * 3 + 2]) {
            float t = -m[0 * 3 + 0] + m[1 * 3 + 1] - m[2 * 3 + 2] + 1.0f;
            float s = 1.0f / sqrtf(t) * 0.5f;
            qy = s * t;
            qx = (m[0 * 3 + 1] + m[1 * 3 + 0]) * s;
            qw = (m[2 * 3 + 0] - m[0 * 3 + 2]) * s;
            qz = (m[1 * 3 + 2] + m[2 * 3 + 1]) * s;
        } else {
            float t = -m[0 * 3 + 0] - m[1 * 3 + 1] + m[2 * 3 + 2] + 1.0f;
            float s = 1.0f / sqrtf(t) * 0.5f;
            qz = s * t;
            qw = (m[0 * 3 + 1] - m[1 * 3 + 0]) * s;
            qx = (m[2 * 3 + 0] + m[0 * 3 + 2]) * s;
            qy = (m[1 * 3 + 2] + m[2 * 3 + 1]) * s;
        }
        
        rotation = {qx, qy, qz, qw};
    } else {
        // Use TRS components directly
        MatrixFromTRS(translation, rotation, scale, localTransform);
    }
    
    float globalTransform[16];
    MatrixMultiply(parentTransform, localTransform, globalTransform);

    // Store node info FIRST (before ProcessMesh, so we can set the model pointer)
    Node nodeInfo;
    nodeInfo.name = node.name;
    nodeInfo.parentIndex = parentIdx;
    nodeInfo.children = node.children;
    
    // Store TRS values (either direct from node or decomposed from matrix)
    nodeInfo.translation = translation;
    nodeInfo.rotation = rotation;
    nodeInfo.scale = scale;
    
    // Process mesh if node has one (creates KeyboardModel with surfaces)
    if (node.mesh >= 0 && node.mesh < model.meshes.size()) {
        // Get current size before adding new model
        size_t modelIndexBeforeProcessing = outModel.models.size();
        
        ProcessMesh(model, model.meshes[node.mesh], globalTransform, nodeIdx, outModel);
        
        // Store TEMPORARY model index (pointer will be set later to avoid dangling refs)
        if (outModel.models.size() > modelIndexBeforeProcessing) {
            nodeInfo._tempModelIndex = modelIndexBeforeProcessing;
        }
    }
    
    outModel.nodes.resize(std::max(outModel.nodes.size(), (size_t)nodeIdx + 1));
    outModel.nodes[nodeIdx] = nodeInfo;

    for (int childIdx : node.children) {
        ProcessNode(model, childIdx, nodeIdx, globalTransform, outModel);
    }
}

bool ParseGLB(const uint8_t* data, size_t size, Model& outModel) {
    std::cout << "glTF: Starting ParseGLB with tinygltf, size: " << size << std::endl;
    
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err;
    std::string warn;
    
    // Parse GLB from memory
    bool ret = loader.LoadBinaryFromMemory(&model, &err, &warn, data, size);
    
    if (!warn.empty()) {
        std::cout << "glTF Warning: " << warn << std::endl;
    }
    
    if (!err.empty()) {
        std::cerr << "glTF Error: " << err << std::endl;
    }
    
    if (!ret) {
        std::cerr << "Failed to parse glTF" << std::endl;
        return false;
    }
    
    std::cout << "glTF: Successfully parsed model with " << model.meshes.size() << " meshes" << std::endl;
    
    // Process scene graph
    int sceneIdx = model.defaultScene;
    if (sceneIdx < 0 || sceneIdx >= model.scenes.size()) {
        sceneIdx = 0;
    }
    
    if (model.scenes.empty()) {
        std::cerr << "glTF: No scenes found" << std::endl;
        return false;
    }
    
    const auto& scene = model.scenes[sceneIdx];
    std::cout << "glTF: Processing scene " << sceneIdx << " with " << scene.nodes.size() << " root nodes" << std::endl;
    
    float identity[16];
    MatrixIdentity(identity);
    
    for (int nodeIdx : scene.nodes) {
        ProcessNode(model, nodeIdx, -1, identity, outModel);
    }
    
    std::cout << "glTF: ParseGLB complete, extracted textures" << std::endl;
              
    ParseAnimation(model, outModel);
    std::cout << "glTF: Parsed " << outModel.animations.size() << " animations" << std::endl;
    
    // SECOND PASS: Set all model pointers now that vector won't reallocate
    std::cout << "glTF: Linking model pointers (second pass)..." << std::endl;
    for (auto& node : outModel.nodes) {
        if (node._tempModelIndex >= 0 && node._tempModelIndex < outModel.models.size()) {
            node.model = &outModel.models[node._tempModelIndex];
        }
    }
    
    return true;
}

} // namespace KeyboardGLTF
