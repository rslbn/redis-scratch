#include "../include/avl.h"
#include <assert.h>

static inline uint32_t max(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? rhs : lhs;
}

static void avl_update(AVLNode *node) {
    node->height = 1 + max(avl_height(node->left), avl_height(node->right));
    node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

static uint8_t avl_get_height_diff(AVLNode *node) {
    uintptr_t p = (uintptr_t)node->parent;
    return p & 0b11;
}

static AVLNode *avl_get_parent(AVLNode *node) {
    uintptr_t p = (uintptr_t)node->parent;
    return (AVLNode *)(p & (~0b11));
}

static AVLNode *rot_right(AVLNode *node) {
    AVLNode *parent = node->parent;
    AVLNode *new_node = node->left;
    AVLNode *inner = new_node->right;

    node->left = inner;
    if (inner) {
        inner->parent = node;
    }
    new_node->parent = parent;
    new_node->right = node;
    node->parent = new_node;
    avl_update(node);
    avl_update(new_node);
    return new_node;
}

static AVLNode *rot_left(AVLNode *node) {
    AVLNode *parent = node->parent; // could be null
    AVLNode *new_node = node->right;
    AVLNode *inner = new_node->left;
    
    // node <-> inner
    node->right = inner;
    if (inner) {
        inner->parent = node;
    }
    // parent <- new_node
    new_node->parent = parent; // NOTE: MAY BE NULL
    // new_node <-> node
    new_node->left = node;
    node->parent = new_node;
    // auxiliary data
    avl_update(node);
    avl_update(new_node);
    return new_node;
}

static AVLNode *avl_fix_left(AVLNode *node) {
    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left = rot_left(node->left);
    }
    return rot_right(node);
}

static AVLNode *avl_fix_right(AVLNode *node) {
    if (avl_height(node->right->right) < avl_height(node->right->left)) {
        node->right = rot_right(node->right);
    }
    return rot_left(node);
}

/**
 * Called on an updated node:
 * - propagate auxiliary data.
 * - fix imbalances
 * - return the new root node.
 */
AVLNode *avl_fix(AVLNode *node) {
    while (true) {
        AVLNode **from = &node; // save the fixed subtree
        AVLNode *parent = node->parent; // NOTE: MAY BE NULL
        if (parent) {
            // attach the fixed subtree to the parent
            from = parent->left == node ? &parent->left : &parent->right;
        } // else: save the local variable `node`
        // auxiliary data
        avl_update(node);
        // fix the height difference of 2
        uint32_t l = avl_height(node->left);
        uint32_t r = avl_height(node->right);
        if (l == r + 2) {
            *from = avl_fix_left(node);
        } else if (l + 2 == r) {
            *from = avl_fix_right(node);
        }
        // root node, stop
        if (!parent) {
            return *from;
        }
        node = parent;
    }
}

static AVLNode *avl_del_easy(AVLNode *node) {
    assert(!node->left || !node->right);
    AVLNode *child = node->left ? node->left : node->right;
    AVLNode *parent = node->parent;

    if (child) {
        child->parent = parent;
    }
    if (!parent) {
        return child;
    }

    AVLNode **from = parent->left == node ? &parent->left : &parent->right;

    *from = child;

    // rebalance the updated tree
    return avl_fix(parent);
}

AVLNode *avl_del(AVLNode *node) {
    if (!node->left || !node->right) {
        return avl_del_easy(node);
    }

    // find the successor
    AVLNode *victim = node->right;
    while (victim->left) { // walk to the most left child
        victim = victim->left;
    }

    // detach successor
    AVLNode *root = avl_del_easy(victim);
    // swap with successor
    *victim = *node;
    if (victim->left) {
        victim->left->parent = victim;
    }

    if (victim->right) {
        victim->right->parent = victim;
    }

    AVLNode **from = &root;
    AVLNode *parent = node->parent;

    if (parent) {
        from = parent->left == node ? &parent->left : &parent->right;
    }
    *from = victim;
    return root;
}

AVLNode *avl_offset(AVLNode *node, int64_t offset) {
    int64_t pos = 0;
    while (offset != pos) {
        if (pos < offset  && pos + avl_cnt(node->right) >= offset) {
            // target is inside the right subtree
            node = node->right;
            pos += avl_cnt(node->left) + 1;
        } else if (pos > offset && pos - avl_cnt(node->left) <= offset) {
            // the target is inside the left subtree
            node = node->left;
            pos -= avl_cnt(node->right) + 1;
        } else {
            // go to the parent
            AVLNode *parent = node->parent;
            if (!parent) return NULL;
            if (parent->right == node) {
                pos -= avl_cnt(node->left) + 1;
            } else {
                pos += avl_cnt(node->right) + 1;
            }
            node = parent;
        }
    }
    return node;
}