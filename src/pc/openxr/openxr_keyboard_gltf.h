#ifndef OPENXR_KEYBOARD_GLTF_H
#define OPENXR_KEYBOARD_GLTF_H

#include <cstdint>
#include <vector>
#include <string>
#include <map>

// Minimal glTF structures for keyboard model parsing
namespace KeyboardGLTF {

struct Vec2 {
    float x, y;
};

struct Vec3 {
    float x, y, z;
};

struct Vec4 {
    float x, y, z, w;
};

struct Vertex {
    Vec3 position;
    Vec2 texCoord;
};

struct MorphTarget {
    std::vector<Vec3> position;  // Position deltas (XY for keyboard, Z constant)
    std::vector<Vec3> normal;    // Normal deltas
    std::vector<Vec2> texCoord;  // UV deltas for dynamic text mapping
};

// Forward declaration
struct KeyboardModel;

// Surface represents a renderable primitive (matches Meta's ModelSurface)
struct ModelSurface {
    std::vector<Vertex> vertices;
    std::vector<Vertex> originalVertices; // For CPU morphing
    std::vector<uint16_t> indices;
    std::vector<MorphTarget> morphTargets;
    uint64_t textureId = 0;
    Vec4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
};

// Model represents a collection of surfaces that share weights (matches Meta's Model)
struct KeyboardModel {
    std::string name;
    std::vector<ModelSurface> surfaces;
    std::vector<float> weights; // Shared weights for all surfaces
};

struct TextureInfo {
    uint64_t id;
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> data;
};

struct AnimationSampler {
    std::vector<float> input;  // Times
    std::vector<float> output; // Values (weights)
    // We only support LINEAR for weights for now
};

struct AnimationChannel {
    int samplerIndex;
    int nodeIndex;
    std::string path; // "weights", "scale", "translation", "rotation"
    int additiveWeightIndex = -1; // -1 means update all weights, >=0 means additive update for specific weight
};

struct Animation {
    std::string name;
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;
};

// Node in the scene hierarchy (matches Meta's ModelNode)
struct Node {
    std::string name;
    
    // Hierarchy
    int parentIndex = -1;
    std::vector<int> children;
    
    // Model data (pointer set after all models loaded to avoid dangling pointers)
    KeyboardModel* model = nullptr;
    
    // Transform components (for animations)
    std::vector<double> translation;
    std::vector<double> rotation; // Quaternion (x, y, z, w)
    std::vector<double> scale;
    
    // Weights (if this node has animations that control weights)
    std::vector<float> weights;
    
    
    // TEMPORARY: Used during parsing to track which model this node should link to
    // The actual pointer is set in a second pass after all models are loaded
    int _tempModelIndex = -1;
};

// Top-level model containing all parsed data
struct Model {
    std::vector<KeyboardModel> models; // Models with surfaces (Meta's pattern)
    std::vector<TextureInfo> textures;
    std::vector<Animation> animations;
    std::vector<Node> nodes;
};

// Parse glTF binary format using tinygltf
bool ParseGLB(const uint8_t* data, size_t size, Model& outModel);

// Helper to parse texture URI
bool ParseTextureURI(const std::string& uri, uint64_t& textureId, uint32_t& width, uint32_t& height);

// Matrix helper functions
void MatrixFromTRS(const std::vector<double>& t, const std::vector<double>& r, const std::vector<double>& s, float* out);

} // namespace KeyboardGLTF

#endif // OPENXR_KEYBOARD_GLTF_H
