#ifndef LUMINE_OBJECT_POOL_H
#define LUMINE_OBJECT_POOL_H

#include "base/common.h"

namespace faas::utils {

    template<class T>
    T *DefaultObjectConstructor() {
        return new T();
    }

// SimpleObjectPool is NOT thread-safe
    template<class T>
    class SimpleObjectPool {
    public:
        explicit SimpleObjectPool(std::function<T *()> object_constructor = DefaultObjectConstructor<T>)
                : object_constructor_(object_constructor) {}

        ~SimpleObjectPool() {}

        T *Get() {
            if (free_objs_.empty()) {
                T *new_obj = object_constructor_();
                free_objs_.push_back(new_obj);
                objs_.emplace_back(new_obj);
            }
            DCHECK(!free_objs_.empty());
            T *obj = free_objs_.back();
            free_objs_.pop_back();
            return obj;
        }

        void Return(T *obj) {
            free_objs_.push_back(obj);
        }

    private:
        std::function<T *()> object_constructor_;

        absl::InlinedVector<std::unique_ptr<T>, 16> objs_;
        absl::InlinedVector<T *, 16> free_objs_;


        DISALLOW_COPY_AND_ASSIGN(SimpleObjectPool);
    };

}  // namespace faas

#endif //LUMINE_OBJECT_POOL_H