#ifndef MAP_UPLOAD_DATA_H
#define MAP_UPLOAD_DATA_H

#include "renderer/util/vk.h"

struct MapUploadData {
    vk::DescriptorSetLayout mapLayout;
    vk::DescriptorSet mapSet;
    int num_chunks;
    vk::DescriptorSetLayout objectLayout;
    vk::DescriptorSet objectSet;
    int num_objects;
};

#endif
