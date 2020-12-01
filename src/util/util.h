#ifndef UTIL_H
#define UTIL_H

#include <vector>
#include <mutex>
#include <iostream>

namespace Util {
    
    std::vector<char> readFile(char const* filename);
    
    template<typename T>
    std::vector<T> nTimes(int n, T t) {
        std::vector<T> vec(n);
        for(int i = 0; i<n; i++) {
            vec[i] = t;
        }
        return vec;
    }
    
    template<typename T>
    int makeSet(std::vector<T> vec) {
        int num = 1;
        
        for(int i = 1; i < vec.size(); i++) {
            bool unique = true;
            
            for(int j = 0; j < num; j++) {
                
                if(vec[i] == vec[j]) {
                    unique = false;
                    break;
                }
                
            }
            
            if(unique) {
                if(num != i) vec[num] = vec[i];
                num++;
            }
        }
        
        return num;
    }
    
    template<typename T>
    int removeElement(std::vector<T> vec, T element) {
        int index = 0;
        
        for(int i = 0; i < vec.size(); i++) {
            
            if(vec[i] != element) {
                if(index != i) vec[index] = vec[i];
                index++;
            }
            
        }
        
        return index;
    }
    
    template<typename T>
    T sq(T v) {
        return v*v;
    }
    
    template<typename T>
    T s_sq(T v) {
        return std::abs(v)*v;
    }
    
    template<typename T>
    constexpr T c_sq(T v) {
        return v*v;
    }
    
    constexpr int c_pow(int down, int up) {
        int mul = 1;
        for(int i = 0; i < up; i++) {
            mul *= down;
        }
        return mul;
    }
    
    template<typename T>
    class ThreadSafe {
    public:
        class Locker {
        public:
            Locker(T* value, std::mutex& mutex) : value(value), mutex(mutex) {
                mutex.lock();
            }
            T* operator-> () {
                return value;
            }
            operator T&() {
                return *value;
            }
            ~Locker() {
                mutex.unlock();
            }
        private:
            T* value;
            std::mutex& mutex;
        };
        template<typename... Args>
        ThreadSafe(Args&&... args) : value(std::forward<Args>(args)...) {}
        Locker operator *() {
            return Locker(&value, mutex);
        }
        operator Locker() {
            return Locker(&value, mutex);
        }
        operator T() {
            return Locker(&value, mutex);
        }
        
    private:
        T value;
        std::mutex mutex;
    };
    
    // 0:a -> 1:b
    inline float lerp(float a, float b, float r) {
        return a * (1.f - r) + b * r;
    }
    
};

#endif
