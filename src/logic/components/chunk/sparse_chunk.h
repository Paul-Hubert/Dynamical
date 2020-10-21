#ifndef SPARSE_CHUNK_H
#define SPARSE_CHUNK_H

#include <functional>
#include <cereal/types/array.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/bitset.hpp>

#include <cmath>
#include "global_chunk_data.h"

const int mhsize = ((int) std::pow(2, std::ceil(std::log2(chunk::max_num_values.x)))/2.);

constexpr float out_of_range = 1000.f;

union indval {
    int ind;
    float val;
    template <class Archive>
    void serialize( Archive & ar ) {
        ar(ind);
    }
};

class SparseChunk {
    
    class Node {
    public:
        
        Node() {}
        
        void get(SparseChunk* tree, GlobalChunkData& cd, int x, int y, int z, int hsize) {
            
            if(hsize == 1) {
                
                int i = 0;
                for(int zi = 0; zi<2; zi++) {
                    for(int yi = 0; yi<2; yi++) {
                        for(int xi = 0; xi<2; xi++) {
                            
                            if(x + xi < chunk::max_num_values.x && y + yi < chunk::max_num_values.y && z + zi < chunk::max_num_values.z) {
                                cd.data[(x+xi) * chunk::max_num_values.y * chunk::max_num_values.z + (z+zi) * chunk::max_num_values.y + (y+yi)]
                                = a[i++].val;
                            }
                            
                        }
                    }
                }
                
            } else {
                
                int index = 0;
                for(int zi = 0; zi<2; zi++) {
                    for(int yi = 0; yi<2; yi++) {
                        for(int xi = 0; xi<2; xi++) {
                            
                            if(empty[index]) {
                                
                                if(a[index].val != out_of_range) set(cd, x + xi*hsize, y + yi*hsize, z + zi*hsize, hsize/2, a[index].val);
                                
                            } else {
                                
                                tree->nodes[a[index].ind].get(tree, cd, x + xi*hsize, y + yi*hsize, z + zi*hsize, hsize/2);
                                
                            }
                            index++;
                            
                        }
                    }
                }
                
            }
            
        }
        
        float get(SparseChunk* tree, int x, int y, int z, int hsize) {
            int index = (z>=hsize) * 4 + (y>=hsize) * 2 + (x>=hsize);
            if(empty[index] || hsize == 1) {
                return a[index].val;
            } else {
                return tree->nodes[a[index].ind].get(tree, x, y, z, hsize/2.);
            }
        }
        
        void set(GlobalChunkData& cd, int x, int y, int z, int hsize, float value) {
            
            for(int zi = 0; zi<hsize*2; zi++) {
                for(int yi = 0; yi<hsize*2; yi++) {
                    for(int xi = 0; xi<hsize*2; xi++) {
                        
                        if(x + xi < chunk::max_num_values.x && y + yi < chunk::max_num_values.y && z + zi < chunk::max_num_values.z) {
                            
                            /*
                            float x1 = Util::lerp(val[0], val[1], xi/(hsize*2-1));
                            float x2 = Util::lerp(val[2], val[3], xi/(hsize*2-1));
                            float x3 = Util::lerp(val[4], val[5], xi/(hsize*2-1));
                            float x4 = Util::lerp(val[6], val[7], xi/(hsize*2-1));
                            
                            float y1 = Util::lerp(x1, x2, yi/(hsize*2-1));
                            float y2 = Util::lerp(x3, x4, yi/(hsize*2-1));
                            
                            float value = Util::lerp(y1, y2, zi/(hsize*2-1));
                            */
                            
                            cd.data[(x+xi) * chunk::max_num_values.y * chunk::max_num_values.z + (z+zi) * chunk::max_num_values.y + (y+yi)]
                            = value;
                            
                        }
                        
                    }
                }
            }
        }
        
        template <class Archive>
        void serialize( Archive & ar ) {
            ar(empty, a);
        }
        
        std::bitset<8> empty;
        std::array<indval, 8> a;
        
    };
    
public:
    
    SparseChunk() {
        
    }
    
    void get(GlobalChunkData& data) {
        nodes[root].get(this, data, 0, 0, 0, mhsize);
    }
    
    void set(GlobalChunkData& cd) {
        nodes.clear();
        nodes.resize(1);
        index = 0;
        root = make_node();
        set(root, cd, 0, 0, 0, mhsize);
        
        nodes.resize(index+1);
        nodes.shrink_to_fit();
        
    }
    
    float get(int x, int y, int z) {
        return nodes[root].get(this, x, y, z, mhsize);
    }
    
    int make_node() {
        int i = index;
        if(index == nodes.size()) {
            index++;
            nodes.push_back(Node());
        } else if(index < nodes.size()) {
            index++;
            nodes[i] = Node();
        }
        return i;
    }
    
    float set(int ind, GlobalChunkData& cd, int x, int y, int z, int hsize) {
        
        if(hsize > 1) {
            
            float sum = 0.f;
            bool sign;
            bool empty = true;
            int i = 0;
            for(int zi = 0; zi<2; zi++) {
                for(int yi = 0; yi<2; yi++) {
                    for(int xi = 0; xi<2; xi++) {
                        int index = make_node();
                        float val = set(index, cd, x + xi*hsize, y + yi*hsize, z + zi*hsize, hsize/2);
                        
                        nodes[ind].empty[i] = (val != 0.0f);
                        if(nodes[ind].empty[i]) {
                            
                            nodes[ind].a[i].val = val;
                            set_index(index);
                            
                        } else {
                            
                            empty = false;
                            nodes[ind].a[i].ind = index;
                            
                        }
                        
                        if(empty) {
                            
                            if(i == 0) {
                                sign = std::signbit(val);
                                sum = val;
                            } else {
                                if(sign != std::signbit(val))
                                    empty = false;
                                if(std::abs(val) < std::abs(sum))
                                    sum = val;
                            }
                            
                        }
                        
                        i++;
                    }
                }
            }
            
            if(empty) {
                return sum;
            } else {
                return 0.f;
            }
            
        } else {
            
            float sum = 0.0f;
            bool sign;
            bool empty = true;
            int i = 0;
            
            int num = 0;
            
            for(int zi = 0; zi<2; zi++) {
                for(int yi = 0; yi<2; yi++) {
                    for(int xi = 0; xi<2; xi++) {
                        
                        float val = out_of_range;
                        if(x + xi < chunk::max_num_values.x && y + yi < chunk::max_num_values.y && z + zi < chunk::max_num_values.z) {
                            
                            val = cd.data[(x + xi) * chunk::max_num_values.y * chunk::max_num_values.z + (z + zi) * chunk::max_num_values.y + (y+yi)];
                            
                            if(num == 0) {
                                sign = std::signbit(val);
                                sum = val;
                            } else {
                                if(sign != std::signbit(val))
                                    empty = false;
                                if(std::abs(val) < std::abs(sum))
                                    sum = val;
                            }
                            
                            num++;
                            
                        }
                        
                        nodes[ind].a[i].val = val;
                        i++;
                    }
                }
            }
            
            if(empty) {
                return num == 0 ? out_of_range : sum;
            } else {
                return 0.f;
            }
            
        }
    }
    
    void set_index(int i) {
        index = i;
    }
    
    template <class Archive>
    void serialize( Archive & ar ) {
        ar(x0, y0, z0, root, nodes);
    }
    
    std::vector<Node> nodes;
    
private:
    int root = 0;
    
    int x0 = 0;
    int y0 = 0;
    int z0 = 0;
    
    int index = 0;

};


#endif
