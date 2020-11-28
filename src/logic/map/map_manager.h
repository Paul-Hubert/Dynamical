#ifndef MAP_MANAGER_H
#define MAP_MANAGER_H

#include <string>

class MapManager {
public:
	MapManager();
	void load(std::string name);
	void unload();
	~MapManager();
private:

};

#endif