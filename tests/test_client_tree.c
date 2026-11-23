/*
	(c) Copyright  2012 - 2026 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, version 3.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/* Co-authored with Claude Sonnet 4.6 */

#include "../PICA_node.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//extern int client_tree_add(struct client *ci);
//extern struct client *client_tree_search(const unsigned char *id);
//extern void client_tree_remove(struct client *ci);
//extern struct client *client_tree_root;
struct client *client_tree_root = NULL;

int client_tree_add(struct client *ci)
{
	struct client *i;
	if (client_tree_root)
	{
		i = client_tree_root;

		do
		{
			int cmp;

			cmp = memcmp(ci->id, i->id, PICA_ID_SIZE);

			if (cmp == 0)
				return 0;

			if (cmp < 0) // ci->id < i->id
			{
				if (i->left)
					i = i->left;
				else
				{
					i->left = ci;
					ci->up = i;
					return 1;
				}
			}
			else
			{
				if (i->right)
					i = i->right;
				else
				{
					i->right = ci;
					ci->up = i;
					return 1;
				}
			}
		}
		while(1);
	}
	else
		client_tree_root = ci;

	return 1;
}

struct client* client_tree_search(const unsigned char *id)
{
	struct client* i_ptr;
	i_ptr = client_tree_root;


	while(i_ptr)
	{
		int cmp = memcmp(id, i_ptr->id, PICA_ID_SIZE);

		if (cmp == 0)
			return i_ptr;

		if (cmp < 0) //id < i_ptr->id
			i_ptr = i_ptr->left;
		else
			i_ptr = i_ptr->right;
	}

	return 0;
}

void client_tree_print(struct client *c)
{
	char id_buf[2 * PICA_ID_SIZE];
	if (!c)
	{
		return;
	}


	if (c->next_multi)
		client_tree_print(c->next_multi);

	if (c->left)
		client_tree_print(c->left);

	if (c->right)
		client_tree_print(c->right);
}


void client_tree_remove(struct client* ci)
{
	struct client** p_link;//указывает на left или right в родительском узле

	if (!ci->up)
	{
		p_link = &client_tree_root;
	}
	else
	{
		if (ci == ci->up->left)
			p_link = &(ci->up->left);
		else
			p_link = &(ci->up->right);
	}


	if (!ci->left && !ci->right)
	{
		*p_link = 0;
		return;
	}

	if (ci->left && !ci->right)
	{
		ci->left->up = ci->up;
		*p_link = ci->left;
		return;
	}

	if (!ci->left && ci->right)
	{
		ci->right->up = ci->up;
		*p_link = ci->right;
		return;
	}


	{
		struct client *lm;
		lm = ci->right;
		while(lm->left) lm = lm->left; //поиск самого левого узла правого поддерева

		if (lm != ci->right)
		{
			lm->up->left = lm->right;
			if (lm->right)
				lm->right->up = lm->up; // FIX 1
			lm->right = ci->right;
			ci->right->up = lm; // FIX 2
		}
		lm->left = ci->left;
		if (ci->left)
			ci->left->up = lm; // FIX 3
		lm->up = ci->up;
		*p_link = lm;
	}
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
 
/* Allocate a zeroed node with a single-byte key padded to PICA_ID_SIZE */
static struct client *make_node(unsigned char key)
{
    struct client *n = calloc(1, sizeof(struct client));
    n->id[0] = key;   /* remaining bytes are 0 – still unique for key != 0 */
    return n;
}
 
/* Reset the global tree root */
static void reset_tree(void) { client_tree_root = NULL; }
 
/* Walk every node in the tree and verify ->up consistency */
static int check_up_pointers(struct client *node, struct client *expected_parent)
{
    if (!node) return 1;
    if (node->up != expected_parent) return 0;
    return check_up_pointers(node->left,  node) &&
           check_up_pointers(node->right, node);
}
 
/* Simple test accounting */
static int passed = 0, failed = 0;
static void report(const char *name, int ok)
{
    if (ok) { printf("[PASS] %s\n", name); passed++; }
    else     { printf("[FAIL] %s\n", name); failed++; }
}


/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/*
 * TEST for BUG 1
 * --------------
 * lm->right->up must be updated when lm is detached from its parent.
 *
 * Tree shape built:
 *
 *        50 (root)
 *       /  \
 *     20    80          <- ci = 50 will be removed
 *            \
 *            70         <- right child of 50
 *           /  \
 *          60   75      <- lm = 60 (leftmost of right subtree)
 *                \
 *                73     <- lm->right; its ->up must point to lm->up (70) after removal
 *
 * After removing 50, lm (60) replaces it.
 * lm->right (73) must have ->up == 70, not the stale lm.
 *
 * Note: to keep the shape simple we use keys so that BST ordering holds.
 * (all keys are single bytes compared with memcmp over 32 bytes, so
 *  byte[0] alone determines order when all other bytes are 0.)
 */
static void test_fix1_lm_right_up(void)
{
    reset_tree();

    struct client *n50 = make_node(50);
    struct client *n20 = make_node(20);
    struct client *n80 = make_node(80);  /* unused but keeps shape symmetric */
    struct client *n70 = make_node(70);
    struct client *n60 = make_node(60);
    struct client *n75 = make_node(75);
    struct client *n73 = make_node(73);

    client_tree_add(n50);
    client_tree_add(n20);
    client_tree_add(n80);
    client_tree_add(n70);
    client_tree_add(n60);
    client_tree_add(n75);
    client_tree_add(n73);

    /*
     * Remove n50 (two children).
     * lm = leftmost of right subtree of n50 = n60 (n50->right=n80... wait,
     * we need lm to be deep enough to have a right child.
     *
     * Re-think: use a simpler layout where n50's right child IS the node
     * whose leftmost descendant has a right child.
     *
     * Rebuild with clearer shape:
     *   root = 50
     *   50->left  = 20
     *   50->right = 70    (ci->right)
     *   70->left  = 60    (lm – leftmost of right subtree)
     *   60->right = 65    (lm->right – the node whose ->up is tested)
     */
    reset_tree();
    free(n80); free(n75); free(n73);

    n50 = make_node(50);
    n20 = make_node(20);
    n70 = make_node(70);
    n60 = make_node(60);
    struct client *n65 = make_node(65);  /* lm->right */

    client_tree_add(n50);  /* root              */
    client_tree_add(n20);  /* 50->left          */
    client_tree_add(n70);  /* 50->right         */
    client_tree_add(n60);  /* 70->left  = lm    */
    client_tree_add(n65);  /* 60->right = lm->right */

    /* Sanity: verify shape before removal */
    /* n50 is root, n70 is ci->right, n60 is lm, n65 is lm->right */

    client_tree_remove(n50);

    /*
     * After removal, lm (n60) is the new root (n50->up was NULL).
     * lm->right should now be n70 (ci->right).
     * n65 (old lm->right) was detached from n60 and re-hung under n70.
     * Specifically: n70->left = n65, so n65->up must == n70.
     */
    int ok = (n65->up == n70);
    report("FIX1: lm->right->up updated after lm detached from parent", ok);

    free(n50); free(n20); free(n70); free(n60); free(n65);
}

/*
 * TEST for BUG 2
 * --------------
 * ci->right->up must be updated to lm when lm != ci->right.
 *
 * Same tree shape as above.  After removing n50:
 *   lm (n60) takes n50's position as root.
 *   lm->right is set to ci->right (n70).
 *   Therefore n70->up must == n60 (lm).
 */
static void test_fix2_ci_right_up(void)
{
    reset_tree();

    struct client *n50 = make_node(50);
    struct client *n20 = make_node(20);
    struct client *n70 = make_node(70);
    struct client *n60 = make_node(60);
    struct client *n65 = make_node(65);

    client_tree_add(n50);
    client_tree_add(n20);
    client_tree_add(n70);
    client_tree_add(n60);
    client_tree_add(n65);

    client_tree_remove(n50);

    /*
     * lm = n60 (leftmost of n50->right = n70's subtree).
     * lm != ci->right (n60 != n70), so the "lm != ci->right" branch fires.
     * lm->right is then set to n70, so n70->up must be n60.
     */
    int ok = (n70->up == n60);
    report("FIX2: ci->right->up updated when lm replaces ci", ok);

    free(n50); free(n20); free(n70); free(n60); free(n65);
}

/*
 * TEST for BUG 3
 * --------------
 * ci->left->up must be updated to lm after lm takes ci's position.
 *
 * Minimal two-children removal where lm == ci->right (the simplest case –
 * ci->right has no left child), to isolate Bug 3 independently of Bug 1/2.
 *
 * Tree:
 *        50 (root, ci)
 *       /  \
 *     20    70          lm == ci->right == 70 (no left child)
 *
 * After removing 50, n70 becomes root.
 * n70->left = n20, so n20->up must == n70.
 */
static void test_fix3_ci_left_up(void)
{
    reset_tree();

    struct client *n50 = make_node(50);
    struct client *n20 = make_node(20);
    struct client *n70 = make_node(70);

    client_tree_add(n50);
    client_tree_add(n20);
    client_tree_add(n70);

    client_tree_remove(n50);

    /*
     * lm == n70 (== ci->right, no left child under 70).
     * Code goes to the shared part: lm->left = ci->left (n20).
     * ci->left->up (n20->up) must be updated to lm (n70).
     */
    int ok = (n20->up == n70);
    report("FIX3: ci->left->up updated when lm replaces ci", ok);

    free(n50); free(n20); free(n70);
}

/*
 * INTEGRITY TEST
 * --------------
 * After a sequence of insertions and removals, verify that every ->up
 * pointer in the remaining tree is consistent with the actual parent.
 * This catches all three bugs simultaneously via a post-condition check.
 *
 * Sequence:
 *   Insert: 50, 30, 70, 20, 40, 60, 80, 35, 45, 65
 *   Remove:  50  (two children, lm = 60, lm != 70, lm has no right child)
 *            30  (two children, lm = 35)
 *   Expected: fully consistent ->up pointers throughout.
 */
static void test_full_up_consistency(void)
{
    reset_tree();

    unsigned char keys[] = {50, 30, 70, 20, 40, 60, 80, 35, 45, 65};
    int n = sizeof(keys) / sizeof(keys[0]);
    struct client *nodes[10];

    for (int i = 0; i < n; i++) {
        nodes[i] = make_node(keys[i]);
        client_tree_add(nodes[i]);
    }

    /* Remove node with key 50 (two children; lm=60 which has right child 65) */
    client_tree_remove(nodes[0]);   /* n50 */

    /* Remove node with key 30 (two children; lm=35 which has no right child) */
    client_tree_remove(nodes[1]);   /* n30 */

    int ok = check_up_pointers(client_tree_root, NULL);
    report("INTEGRITY: all ->up pointers consistent after multiple removals", ok);

    for (int i = 0; i < n; i++) free(nodes[i]);
}


int main(void)
{
    test_fix1_lm_right_up();
    test_fix2_ci_right_up();
    test_fix3_ci_left_up();
    test_full_up_consistency();
 
    printf("\n%d passed, %d failed.\n", passed, failed);
    return failed ? 1 : 0;
}
