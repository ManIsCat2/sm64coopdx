#include "openxr_swapchain.h"
#include <iostream>
#include <vector>
#include <cstdlib>
#include <cstring>

using namespace std;

int getViewConfiguration(
    XrInstance instance,
    XrSystemId systemId,
    uint32_t* viewCount,
    XrViewConfigurationView* views
)
{
    uint32_t count = 2;  // Stereo has 2 views
    
    XrResult result = xrEnumerateViewConfigurationViews(
        instance,
        systemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        count,
        &count,
        views
    );
    
    if (result != XR_SUCCESS) {
        cerr << "Failed to enumerate view configuration views: " << result << endl;
        return 0;
    }
    
    *viewCount = count;
    return 1;
}

int createOpenXRSwapchains(
    XrInstance instance,
    XrSystemId systemId,
    XrSession session,
    OpenXRSwapchain** leftSwapchain,
    OpenXRSwapchain** rightSwapchain
)
{
    // Get view configuration
    XrViewConfigurationView configViews[2] = {
        {XR_TYPE_VIEW_CONFIGURATION_VIEW},
        {XR_TYPE_VIEW_CONFIGURATION_VIEW}
    };
    
    uint32_t viewCount = 0;
    if (!getViewConfiguration(instance, systemId, &viewCount, configViews)) {
        return 0;
    }
    
    if (viewCount != 2) {
        cerr << "Expected 2 views for stereo, got " << viewCount << endl;
        return 0;
    }
    
    // Enumerate swapchain formats
    uint32_t formatCount = 0;
    XrResult result = xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr);
    
    if (result != XR_SUCCESS) {
        cerr << "Failed to enumerate swapchain formats: " << result << endl;
        return 0;
    }
    
    vector<int64_t> formats(formatCount);
    result = xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data());
    
    if (result != XR_SUCCESS) {
        cerr << "Failed to enumerate swapchain formats: " << result << endl;
        return 0;
    }
    
    // Choose format - prefer SRGB for color accuracy
    int64_t chosenFormat = formats[0];
    for (int64_t format : formats) {
        if (format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB) {
            chosenFormat = format;
            break;
        }
    }
    
    cout << "Selected swapchain format: " << chosenFormat << endl;
    
    // Create swapchains for both eyes
    for (int i = 0; i < 2; i++) {
        OpenXRSwapchain* sc = (OpenXRSwapchain*)calloc(1, sizeof(OpenXRSwapchain));
        if (!sc) {
            cerr << "Failed to allocate swapchain structure" << endl;
            return 0;
        }
        
        sc->format = (VkFormat)chosenFormat;
        sc->width = configViews[i].recommendedImageRectWidth;
        sc->height = configViews[i].recommendedImageRectHeight;
        
        cout << "Creating swapchain for eye " << i 
             << " with resolution " << sc->width << "x" << sc->height << endl;
        
        // Create swapchain
        XrSwapchainCreateInfo swapchainCreateInfo{};
        swapchainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | 
                                         XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                                         XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        swapchainCreateInfo.format = chosenFormat;
        swapchainCreateInfo.sampleCount = 1;
        swapchainCreateInfo.width = sc->width;
        swapchainCreateInfo.height = sc->height;
        swapchainCreateInfo.faceCount = 1;
        swapchainCreateInfo.arraySize = 1;
        swapchainCreateInfo.mipCount = 1;
        
        result = xrCreateSwapchain(session, &swapchainCreateInfo, &sc->swapchain);
        
        if (result != XR_SUCCESS) {
            cerr << "Failed to create swapchain for eye " << i << ": " << result << endl;
            free(sc);
            return 0;
        }
        
        // Get swapchain images
        uint32_t imageCount = 0;
        result = xrEnumerateSwapchainImages(sc->swapchain, 0, &imageCount, nullptr);
        
        if (result != XR_SUCCESS) {
            cerr << "Failed to enumerate swapchain images: " << result << endl;
            xrDestroySwapchain(sc->swapchain);
            free(sc);
            return 0;
        }
        
        vector<XrSwapchainImageVulkanKHR> swapchainImages(imageCount);
        for (uint32_t j = 0; j < imageCount; j++) {
            swapchainImages[j].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
            swapchainImages[j].next = nullptr;
        }
        
        result = xrEnumerateSwapchainImages(
            sc->swapchain,
            imageCount,
            &imageCount,
            (XrSwapchainImageBaseHeader*)swapchainImages.data()
        );
        
        if (result != XR_SUCCESS) {
            cerr << "Failed to get swapchain images: " << result << endl;
            xrDestroySwapchain(sc->swapchain);
            free(sc);
            return 0;
        }
        
        sc->imageCount = imageCount;
        sc->images = (VkImage*)calloc(imageCount, sizeof(VkImage));
        if (!sc->images) {
            cerr << "Failed to allocate image array" << endl;
            xrDestroySwapchain(sc->swapchain);
            free(sc);
            return 0;
        }
        
        for (uint32_t j = 0; j < imageCount; j++) {
            sc->images[j] = swapchainImages[j].image;
        }
        
        cout << "Created swapchain with " << imageCount << " images" << endl;
        
        // Assign to output
        if (i == 0) {
            *leftSwapchain = sc;
        } else {
            *rightSwapchain = sc;
        }
    }
    
    return 1;
}

void destroyOpenXRSwapchain(OpenXRSwapchain* swapchain)
{
    if (!swapchain) return;
    
    if (swapchain->swapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(swapchain->swapchain);
    }
    
    if (swapchain->images) {
        free(swapchain->images);
    }
    
    free(swapchain);
}

