#ifndef QUAD_TREE_H
#define QUAD_TREE_H

#include <functional>
#include <cereal/types/memory.hpp>
#include <cereal/types/array.hpp>

#include <cmath>

namespace dy {

template <typename T>
class QuadTree {
    
    class Node {
    public:
        
        Node() : hsize(0), value() {}
        
        Node(int size) : hsize(size), value() {
            for(int i = 0; i<8; i++) {
                ptrs[i] = nullptr;
            }
        }
        
        Node(T value) : hsize(0), value(value) {}
        
        T get(int x, int y, int z, int mhsize) {
            if(hsize > mhsize && !value) {
                int index = (x>=hsize?1:0) + (y>=hsize?2:0) + (z>=hsize?4:0);
                if(ptrs[index] == nullptr) {
                    return T();
                }
                return ptrs[index]->get(x-(x>=hsize?hsize:0), y-(y>=hsize?hsize:0), z-(z>=hsize?hsize:0), mhsize);
            } else {
                return value;
            }
        }
    
        void set(int x, int y, int z, int mhsize, T val) {
            if(hsize > mhsize) {
                int index = (x>=hsize?1:0) + (y>=hsize?2:0) + (z>=hsize?4:0);
                if(ptrs[index] == nullptr) {
                    ptrs[index] = std::make_unique<Node>(hsize/2);
                }
                ptrs[index]->set(x-(x>=hsize?hsize:0), y-(y>=hsize?hsize:0), z-(z>=hsize?hsize:0), mhsize, val);
            } else {
                value = val;
            }
        }
        
        bool remove(int x, int y, int z) {
            if(hsize>0) {
                int index = (x>=hsize?1:0) + (y>=hsize?2:0) + (z>=hsize?4:0);
                if(ptrs[index] == nullptr || !ptrs[index]->remove(x-(x>=hsize?hsize:0), y-(y>=hsize?hsize:0), z-(z>=hsize?hsize:0))) {
                    ptrs[index] = nullptr;
                    int c = 0;
                    for(int i = 0; i<8; i++) {
                        if(ptrs[index] != nullptr) {
                            c++;
                        }
                    }
                    if(c == 0) {
                        return false;
                    }
                    return true;
                }
                
            }
            return false;
        }
        
        void set(int index, std::unique_ptr<Node>& val) {
            ptrs[index] = std::move(val);
        }

        int hsize;
        
        template <class Archive>
        void serialize( Archive & ar ) {
            ar(value, ptrs);
        }
        
    private:
        
        T value;
        std::array<std::unique_ptr<Node>, 8> ptrs;
    };
    
public:
    
    QuadTree(T null_value) {
        root = std::make_unique<Node>((int) std::pow(2, 0));
    }
    
    T get(int x, int y, int z, int mhsize = 0) {
        if(root->hsize < mhsize || std::max(std::max(x+x0, y+y0), z+z0) >= root->hsize*2 || std::min(std::min(x+x0, y+y0), z+z0) < 0) return T();
        return root->get(x+x0, y+y0, z+z0, mhsize);
    }
    
    void set(int x, int y, int z, int mhsize, T val) {
        
        if(root->hsize < mhsize || std::max(std::max(x+x0, y+y0), z+z0) >= root->hsize*2 || std::min(std::min(x+x0, y+y0), z+z0) < 0) { // Vérifier que l'arbre est assez grand.
            
            std::unique_ptr<Node> node = std::make_unique<Node>((int) std::max(root->hsize*2, 1));
            
            node->set(((x+x0)<0?1:0) + ((y+y0)<0?2:0) + ((z+z0)<0?4:0), root);
            root = std::move(node); // Remplacer root par un Node plus grand.
            
            x0 += (x+x0<0?root->hsize:0);
            y0 += (y+y0<0?root->hsize:0);
            z0 += (z+z0<0?root->hsize:0);
            
            return set(x, y, z, mhsize, val); // Recommencer tant que l'arbre n'est pas assez grand.
        } else return root->set(x+x0, y+y0, z+z0, mhsize, val);
        
    }
    
    void remove(int x, int y, int z) {
        if(std::max(std::max(x+x0, y+y0), z+z0) >= root->hsize*2 || std::min(std::min(x+x0, y+y0), z+z0) < 0) { // Vérifier que l'arbre est assez grand.
            
            root->remove(x, y, z);
            
        }
        
    }
    
    void print() {
        
        for(int y = 0; y < root->hsize*2; y++) {
            for(int x = 0; x < root->hsize*2; x++) {
                if(x == x0 && y == y0) {
                    std::cout << "- ";
                } else {
                    //T a = *root->get(x, y);
                    //std::cout << (a == null ? 0 : 1) << " ";
                }
            }
            std::cout << std::endl;
        }
        
    }
    
    template <class Archive>
    void serialize( Archive & ar ) {
        ar(x0, y0, z0, root);
    }
    
private:
    std::unique_ptr<Node> root;
    int x0 = 0;
    int y0 = 0;
    int z0 = 0;

};

}

#endif
