#include "context.h"

Context::Context(entt::registry& reg) : win(*this), instance(*this), device(*this), transfer(*this), swap(*this) {

    transfer.flush();

}

Context::~Context() {

    

}