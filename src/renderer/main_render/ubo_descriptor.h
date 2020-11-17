#ifndef UBO_DESCRIPTOR_H
#define UBO_DESCRIPTOR_H

#include "renderer/vk.h"

#include "renderer/vmapp.h"

#include "renderer/num_frames.h"

#include "glm/glm.hpp"

struct UBO {
    
    glm::mat4 viewproj;
    
    glm::vec4 viewpos;
    
};

class Device;

class UBODescriptor {
public:
    UBODescriptor(Device& device);
    ~UBODescriptor();
    
    vk::DescriptorPool descPool;
    vk::DescriptorSetLayout descLayout;
    std::vector<vk::DescriptorSet> descSets;
    
    std::array<VmaBuffer, NUM_FRAMES> ubos;
    std::array<UBO*, NUM_FRAMES> pointers;
private:
    Device& device;
    
};

#endif
