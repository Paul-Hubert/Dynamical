#ifndef MESHINSTANCEC_H
#define MESHINSTANCEC_H

class MeshInstanceC {
public:
	MeshInstanceC(entt::entity mesh) : mesh(mesh) {}
	entt::entity mesh;
};

#endif