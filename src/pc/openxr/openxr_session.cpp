#include "openxr_session.h"
#include <iostream>
#include <cmath>

XrSession createXRSession(
    XrInstance instance,
    XrSystemId systemID,
    VkInstance vulkanInstance,
    VkPhysicalDevice physDevice,
    VkDevice device,
    uint32_t queueFamilyIndex
)
{
    XrSession session;

    XrGraphicsBindingVulkanKHR graphicsBinding{};
    graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
    graphicsBinding.instance = vulkanInstance;
    graphicsBinding.physicalDevice = physDevice;
    graphicsBinding.device = device;
    graphicsBinding.queueFamilyIndex = queueFamilyIndex;
    graphicsBinding.queueIndex = 0;

    XrSessionCreateInfo sessionCreateInfo{};
    sessionCreateInfo.type = XR_TYPE_SESSION_CREATE_INFO;
    sessionCreateInfo.next = &graphicsBinding;
    sessionCreateInfo.createFlags = 0;
    sessionCreateInfo.systemId = systemID;

    XrResult result = xrCreateSession(instance, &sessionCreateInfo, &session);

    if (result != XR_SUCCESS)
    {
        std::cerr << "Failed to create OpenXR session: " << result << std::endl;
        return XR_NULL_HANDLE;
    }

    std::cout << "OpenXR session created successfully" << std::endl;

    return session;
}

void destroyXRSession(XrSession session)
{
    if (session != XR_NULL_HANDLE) {
        xrDestroySession(session);
    }
}

XrSpace createXRSpace(XrSession session)
{
    XrSpace space;

    XrReferenceSpaceCreateInfo spaceCreateInfo{};
    spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;


    // Rotate 90 degrees to the left (around +Y)
    XrQuaternionf rotation;
    rotation.x = 0.0f;
    rotation.y = -sinf(M_PI / 4.0f);  // +90°/2
    rotation.z = 0.0f;
    rotation.w = cosf(M_PI / 4.0f);

    spaceCreateInfo.poseInReferenceSpace = { { rotation.x, rotation.y, rotation.z, rotation.w }, { 5.0f, -3.0f, 5.0f } };

    XrResult result = xrCreateReferenceSpace(session, &spaceCreateInfo, &space);

    if (result != XR_SUCCESS)
    {
        std::cerr << "Failed to create OpenXR reference space: " << result << std::endl;
        return XR_NULL_HANDLE;
    }

    std::cout << "OpenXR reference space created successfully" << std::endl;

    return space;
}

void destroyXRSpace(XrSpace space)
{
    if (space != XR_NULL_HANDLE) {
        xrDestroySpace(space);
    }
}

