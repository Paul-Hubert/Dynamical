#ifndef COLOR_H
#define COLOR_H

#include "glm/glm.hpp"
#include <stdio.h>

class Color {
public:
    Color() {}
    Color(float r, float g, float b) : rgba(r, g, b, 1.0) {}
    Color(float r, float g, float b, float a) : rgba(r, g, b, a) {}
    Color(const char* str) {
        int r, g, b;
        sscanf(str, "%02x%02x%02x", &r, &g, &b);
        rgba = glm::vec4(r, g, b, 255) / 255.0f;
    }
    glm::vec4 rgba;
};

#endif
