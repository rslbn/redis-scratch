#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <set>
#include <string>
#include "../include/avl.h"


#define container_of(ptr, T, member) \
    ((T *)( (char *)(ptr) - offsetof(T, member) ))


struct Data {
    AVLNode node;
    uint32_t val = 0;
};

struct Container {
    AVLNode *root = NULL;
};

static void add(Container &c, uint32_t val) {
    Data *data = new Data();    // allocate the data
    avl_init(&data->node);
    data->val = val;

    AVLNode *cur = NULL;        // current node
    AVLNode **from = &c.root;   // the incoming pointer to the next node
    while (*from) {             // tree search
        cur = *from;
        uint32_t node_val = container_of(cur, Data, node)->val;
        from = (val < node_val) ? &cur->left : &cur->right;
    }
    *from = &data->node;        // attach the new node
    data->node.parent = cur;
    c.root = avl_fix(&data->node);
}

static bool del(Container &c, uint32_t val) {
    AVLNode *cur = c.root;
    while (cur) {
        uint32_t node_val = container_of(cur, Data, node)->val;
        if (val == node_val) {
            break;
        }
        cur = val < node_val ? cur->left : cur->right;
    }
    if (!cur) {
        return false;
    }

    c.root = avl_del(cur);
    delete container_of(cur, Data, node);
    return true;
}

static void avl_verify(AVLNode *parent, AVLNode *node) {
    if (!node) {
        return;
    }

    assert(node->parent == parent);
    avl_verify(node, node->left);
    avl_verify(node, node->right);

    assert(node->cnt == 1 + avl_cnt(node->left) + avl_cnt(node->right));

    uint32_t l = avl_height(node->left);
    uint32_t r = avl_height(node->right);
    assert(l == r || l + 1 == r || l == r + 1);
    assert(node->height == 1 + std::max(l, r));

    uint32_t val = container_of(node, Data, node)->val;
    if (node->left) {
        assert(node->left->parent == node);
        assert(container_of(node->left, Data, node)->val <= val);
    }
    if (node->right) {
        assert(node->right->parent == node);
        assert(container_of(node->right, Data, node)->val >= val);
    }
}

static void extract(AVLNode *node, std::multiset<uint32_t> &extracted) {
    if (!node) {
        return;
    }
    extract(node->left, extracted);
    extracted.insert(container_of(node, Data, node)->val);
    extract(node->right, extracted);
}

static void container_verify(
    Container &c, const std::multiset<uint32_t> &ref)
{
    avl_verify(NULL, c.root);
    assert(avl_cnt(c.root) == ref.size());
    std::multiset<uint32_t> extracted;
    extract(c.root, extracted);
    assert(extracted == ref);
}

static void dispose(Container &c) {
    while (c.root) {
        AVLNode *node = c.root;
        c.root = avl_del(c.root);
        delete container_of(node, Data, node);
    }
}

static void print_node(AVLNode *node, const std::string &prefix, bool is_left, bool is_root) {
    if (!node) {
        return;
    }
    printf("%s", prefix.c_str());
    if (is_root) {
        printf("── ");
    } else {
        printf("%s", is_left ? "├── (L) " : "└── (R) ");
    }
    uint32_t val = container_of(node, Data, node)->val;
    int32_t balance = (int32_t)avl_height(node->right) - (int32_t)avl_height(node->left);
    printf("%u [h=%u, cnt=%u, bf=%+d]\n", val, node->height, node->cnt, balance);

    std::string child_prefix = prefix + (is_root ? "   " : (is_left ? "│   " : "    "));
    if (node->left && node->right) {
        print_node(node->left, child_prefix, true, false);
        print_node(node->right, child_prefix, false, false);
    } else if (node->left) {
        print_node(node->left, child_prefix, true, false);
        printf("%s└── (R) ~\n", child_prefix.c_str());
    } else if (node->right) {
        printf("%s├── (L) ~\n", child_prefix.c_str());
        print_node(node->right, child_prefix, false, false);
    }
}

static void print_tree(const Container &c, const char *title = NULL) {
    if (title) {
        printf("%s\n", title);
    }
    if (!c.root) {
        printf(" (empty)\n");
    } else {
        print_node(c.root, "", false, true);
    }
}

static void test_insert(uint32_t sz) {
    for (uint32_t val = 0; val < sz; ++val) {
        Container c;
        std::multiset<uint32_t> ref;
        for (uint32_t i = 0; i < sz; ++i) {
            if (i == val) {
                continue;
            }
            add(c, i);
            ref.insert(i);
        }
        container_verify(c, ref);

        add(c, val);
        ref.insert(val);
        container_verify(c, ref);
        dispose(c);
    }
}

static void test_insert_dup(uint32_t sz) {
    for (uint32_t val = 0; val < sz; ++val) {
        Container c;
        std::multiset<uint32_t> ref;
        for (uint32_t i = 0; i < sz; ++i) {
            add(c, i);
            ref.insert(i);
        }
        container_verify(c, ref);

        add(c, val);
        ref.insert(val);
        container_verify(c, ref);
        dispose(c);
    }
}

static void test_remove(uint32_t sz) {
    for (uint32_t val = 0; val < sz; ++val) {
        Container c;
        std::multiset<uint32_t> ref;
        for (uint32_t i = 0; i < sz; ++i) {
            add(c, i);
            ref.insert(i);
        }
        container_verify(c, ref);

        assert(del(c, val));
        ref.erase(val);
        container_verify(c, ref);
        dispose(c);
    }
}

int main() {
    printf("============================================================\n");
    printf("               AVL Tree Visual Demonstration                \n");
    printf("============================================================\n");
    {
        Container demo;
        uint32_t seq[] = {30, 20, 40, 10, 25, 35, 50, 5, 15, 28, 45};
        printf("\n1. Step-by-step insertions:\n");
        for (uint32_t val : seq) {
            printf("\n--> Insert %u:\n", val);
            add(demo, val);
            print_tree(demo);
        }

        printf("\n------------------------------------------------------------\n");
        printf("2. Step-by-step deletions (triggering rebalance & rotations):\n");

        printf("\n--> Delete 5 (leaf node):\n");
        del(demo, 5);
        print_tree(demo);

        printf("\n--> Delete 20 (node with two children):\n");
        del(demo, 20);
        print_tree(demo);

        printf("\n--> Delete 30 (root node):\n");
        del(demo, 30);
        print_tree(demo);

        dispose(demo);
    }

    printf("\n============================================================\n");
    printf("               Running Automated Verification Tests         \n");
    printf("============================================================\n");

    Container c;

    // some quick tests
    printf("[1/5] Running quick base tests...\n");
    container_verify(c, {});
    add(c, 123);
    container_verify(c, {123});
    assert(!del(c, 124));
    assert(del(c, 123));
    container_verify(c, {});

    // sequential insertion
    printf("[2/5] Running sequential insertions (334 nodes)...\n");
    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < 1000; i += 3) {
        add(c, i);
        ref.insert(i);
        container_verify(c, ref);
    }

    // random insertion
    printf("[3/5] Running random insertions (100 nodes)...\n");
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t val = (uint32_t)rand() % 1000;
        add(c, val);
        ref.insert(val);
        container_verify(c, ref);
    }

    // random deletion
    printf("[4/5] Running random deletions (200 ops)...\n");
    for (uint32_t i = 0; i < 200; i++) {
        uint32_t val = (uint32_t)rand() % 1000;
        auto it = ref.find(val);
        if (it == ref.end()) {
            assert(!del(c, val));
        } else {
            assert(del(c, val));
            ref.erase(it);
        }
        container_verify(c, ref);
    }

    // insertion/deletion at various positions
    printf("[5/5] Running stress tests at various sizes (200 iterations)...\n");
    for (uint32_t i = 0; i < 200; ++i) {
        test_insert(i);
        test_insert_dup(i);
        test_remove(i);
    }

    dispose(c);
    printf("\n>>> ALL TESTS PASSED SUCCESSFULLY! <<<\n\n");
    return 0;
}