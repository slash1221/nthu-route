#include "Construct_2d_tree.h"

#include <bits/move.h>
#include <boost/multi_array/base.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <utility>

#include "../flute/flute-function.h"
#include "../flute/flute4nthuroute.h"
#include "../grdb/RoutingComponent.h"
#include "../grdb/RoutingRegion.h"
#include "Congestion.h"
#include "MonotonicRouting.h"
#include "Range_router.h"
#include "Route_2pinnets.h"

using namespace std;

//Sum all demand value on every edge
//So if demand vlaue = wire length, this function can be used
int Construct_2d_tree::cal_total_wirelength() {
    int total_wl = 0;

    for (int i = rr_map.get_gridx() - 2; i >= 0; --i) {
        for (int j = rr_map.get_gridy() - 1; j >= 0; --j) {
            total_wl += (int) congestionMap2d.edge(i, j, RIGHT).cur_cap;
        }
    }
    for (int i = rr_map.get_gridx() - 1; i >= 0; --i) {
        for (int j = rr_map.get_gridy() - 2; j >= 0; --j) {
            total_wl += (int) congestionMap2d.edge(i, j, FRONT).cur_cap;
        }
    }
    printf("total wirelengh:%d\n", total_wl);
    return total_wl;
}

/*sort bbox in ascending order, then pin_num in descending order*/
bool Construct_2d_tree::comp_net(const Net* a, const Net* b) {
    if (a->get_bboxSize() > b->get_bboxSize()) {
        return true;
    } else if (a->get_bboxSize() < b->get_bboxSize()) {
        return false;
    } else {
        return (a->get_pinNumber() < b->get_pinNumber());
    }
}

static bool Two_pin_element::comp_2pin_net(Two_pin_element&a, Two_pin_element&b) {
    int a_bbox_size = abs(a.pin1.x - a.pin2.x) + abs(a.pin1.y - a.pin2.y);
    int b_bbox_size = abs(b.pin1.x - b.pin2.x) + abs(b.pin1.y - b.pin2.y);
    return (a_bbox_size < b_bbox_size);
}

static bool Two_pin_element_2d::comp_2pin_net_from_path(Two_pin_element_2d&a, Two_pin_element_2d&b) {
    int a_bbox_size = abs(a.pin1.x - a.pin2.x) + abs(a.pin1.y - a.pin2.y);
    int b_bbox_size = abs(b.pin1.x - b.pin2.x) + abs(b.pin1.y - b.pin2.y);
    return (a_bbox_size < b_bbox_size);
}

//sort by x,y,pin,steiner
bool Construct_2d_tree::comp_vertex_fl(Vertex_flute_ptr a, Vertex_flute_ptr b) {
    if (a->x < b->x)
        return true;
    else if (a->x > b->x)
        return false;
    else if (a->y < b->y)
        return true;
    else if (a->y > b->y)
        return false;
    else if (a->type == PIN)
        return true;
    else
        return false;
}

/*assign the estimated track# to each edge*/
void Construct_2d_tree::init_2d_map() {

    congestionMap2d(rr_map.get_gridx(), rr_map.get_gridy(), Edge_2d());

    for (int x = rr_map.get_gridx() - 2; x >= 0; --x) {
        for (int y = rr_map.get_gridy() - 1; y >= 0; --y) {
            for (int layer = rr_map.get_layerNumber() - 1; layer >= 0; --layer) {
#ifdef IBM_CASE		
                //There is no wire spacing, so
                //the edge capacity on congestion map = edge capacity on every layer
                congestionMap2d.edge(x, y, DIR_EAST).max_cap += rr_map.capacity(layer, x, y, x+1, y);
#else
                //Because the wire width = 1 and wire spaceing = 1,
                //the edge capacity on congestion map = edge capacity on every layer /2.
                congestionMap2d.edge(x, y, DIR_EAST).max_cap += (rr_map.capacity(layer, x, y, x + 1, y) / 2);
#endif		
            }
        }
    }

    for (int x = rr_map.get_gridx() - 1; x >= 0; --x) {
        for (int y = rr_map.get_gridy() - 2; y >= 0; --y) {
            for (int layer = rr_map.get_layerNumber() - 1; layer >= 0; --layer) {
#ifdef IBM_CASE		
                //There is no wire spacing, so
                //the edge capacity on congestion map = edge capacity on every layer
                congestionMap2d.edge(x, y, DIR_NORTH).max_cap += rr_map.capacity(layer, x, y, x, y+1);
#else
                //Because the wire width = 1 and wire spaceing = 1,
                //the edge capacity on congestion map = edge capacity on every layer /2.
                congestionMap2d.edge(x, y, DIR_NORTH).max_cap += (rr_map.capacity(layer, x, y, x, y + 1) / 2);
#endif		
            }
        }
    }
}

//Make an coordinate array which contains the (x, y) information
void Construct_2d_tree::allocate_coor_array() {
    coor_array.resize(boost::extents[rr_map.get_gridx()][rr_map.get_gridy()]);

}



void Construct_2d_tree::init_2pin_list() {
    int i, netnum;

    netnum = rr_map.get_netNumber();
    for (i = 0; i < netnum; ++i) {
//Two pin nets group by net id. So for fetching the 2nd net's 2-pin net,
//you can fetch by net_2pin_list[2][i], where i is the id of 2-pin net.
        net_2pin_list.push_back(std::vector<Two_pin_element_2d>());
        bbox_2pin_list.push_back(std::vector<Two_pin_element_2d>());
    }
}

void Construct_2d_tree::init_flute() {
    net_flutetree = (Tree *) malloc(rr_map.get_netNumber() * sizeof(Tree));
}

void Construct_2d_tree::free_memory_con2d() {

    bbox_2pin_list.clear();
}

void Construct_2d_tree::bbox_route(Two_pin_list_2d& list, const double value) {
    int i, x1, y1, x2, y2;
    double u_value;

    if (value > 0)
        u_value = 1;
    else
        u_value = -1;

    for (Two_pin_element_2d& it : list) {
        if (it.pin1.x > it.pin2.x) {
            swap(it.pin1.x, it.pin2.x);
        }
        if (it.pin1.y > it.pin2.y) {
            swap(it.pin1.y, it.pin2.y);
        }

        x1 = it.pin1.x;
        y1 = it.pin1.y;
        x2 = it.pin2.x;
        y2 = it.pin2.y;
#ifdef DEBUG_BBOX
        printf("(%d %d) (%d %d)\n",x1,y1,x2,y2);
#endif
        if (x1 == x2)	    //vertical edge
                {
            for (i = y1; i < y2; ++i) {
                bboxRouteStateMap.color(x1, i, DIR_NORTH) = 1;
            }
        } else if (y1 == y2)	//horizontal edge
                {
            for (i = x1; i < x2; ++i) {
                bboxRouteStateMap.color(i, y1, DIR_EAST) = 1;
            }
        } else	            //box (L-shape routing need
        {
            for (i = y1; i < y2; ++i) {
                bboxRouteStateMap.color(x1, i, DIR_NORTH) = 1;
                bboxRouteStateMap.color(x2, i, DIR_NORTH) = 1;
            }
            for (i = x1; i < x2; ++i) {
                bboxRouteStateMap.color(i, y1, DIR_EAST) = 1;
                bboxRouteStateMap.color(i, y2, DIR_EAST) = 1;
            }
        }
    }

//check the flag of edges, if it is set to 1, then add demand on it.
    for (Two_pin_element_2d& it : list) {
//x1, y1 is the smaller set
//NOTE: they have been sorted in the last for loop
        x1 = it.pin1.x;
        y1 = it.pin1.y;
        x2 = it.pin2.x;
        y2 = it.pin2.y;
        if (x1 == x2)	    //vertical edge
                {
            for (i = y1; i < y2; ++i)
                if (bboxRouteStateMap.color(x1, i, DIR_NORTH) == 1) {
                    congestionMap2d.edge(x1, i, DIR_NORTH).cur_cap += u_value;
                    bboxRouteStateMap.color(x1, i, DIR_NORTH) = 0;
                }
        } else if (y1 == y2)	//horizontal edge
                {
            for (i = x1; i < x2; ++i)
                if (bboxRouteStateMap.color(i, y1, DIR_EAST) == 1) {
                    congestionMap2d.edge(i, y1, DIR_EAST).cur_cap += u_value;
                    bboxRouteStateMap.color(i, y1, DIR_EAST) = 0;
                }
        } else	            //box
        {
            for (i = y1; i < y2; ++i) {
                if (bboxRouteStateMap.color(x1, i, DIR_NORTH) == 1) {
                    congestionMap2d.edge(x1, i, DIR_NORTH).cur_cap += value;
                    bboxRouteStateMap.color(x1, i, DIR_NORTH) = 0;
                }
                if (bboxRouteStateMap.color(x2, i, DIR_NORTH) == 1) {
                    congestionMap2d.edge(x2, i, DIR_NORTH).cur_cap += value;
                    bboxRouteStateMap.color(x2, i, DIR_NORTH) = 0;
                }
            }
            for (i = x1; i < x2; ++i) {
                if (bboxRouteStateMap.color(i, y1, DIR_EAST) == 1) {
                    congestionMap2d.edge(i, y1, DIR_EAST).cur_cap += value;
                    bboxRouteStateMap.color(i, y1, DIR_EAST) = 0;
                }
                if (bboxRouteStateMap.color(i, y2, DIR_EAST) == 1) {
                    congestionMap2d.edge(i, y2, DIR_EAST).cur_cap += value;
                    bboxRouteStateMap.color(i, y2, DIR_EAST) = 0;
                }
            }
        }
    }
#ifdef DEBUG_BBOX
    print_cap("cur");
#endif
}

void Construct_2d_tree::insert_all_two_pin_list(Two_pin_element_2d& mn_path_2d) {
    all_two_pin_list.push_back(Two_pin_element());
    Two_pin_element& mn_path = all_two_pin_list.back();

    mn_path.pin1.x = mn_path_2d.pin1.x;
    mn_path.pin1.y = mn_path_2d.pin1.y;
    mn_path.pin2.x = mn_path_2d.pin2.x;
    mn_path.pin2.y = mn_path_2d.pin2.y;
    mn_path.pin1.z = mn_path.pin2.z = 0;
    mn_path.net_id = mn_path_2d.net_id;

}

/*
 input: start coor and end coor, and directions of L
 output: record the best L pattern into two_pin_L_path_global, and return the min max congestion value
 */
Monotonic_element Construct_2d_tree::L_pattern_max_cong(int x1, int y1, int x2, int y2, int dir1, int dir2, Two_pin_element_2d& two_pin_L_path, int net_id) {
    int i, j;
    double temp;
    int dir[2], dir_index;
    Monotonic_element max_path;
    int distance;

    dir[0] = dir1;
    dir[1] = dir2;
    i = x1;
    j = y1;
    max_path.max_cost = -1000000;
    max_path.total_cost = 0;
    max_path.net_cost = 0;
    max_path.distance = 0;
    max_path.via_num = 1;
    for (dir_index = 0; dir_index < 2; dir_index++) {
        if (dir[dir_index] == RIGHT)    // search in horizontal direction: RIGHT
                {
            //for loop from the left boundary to the right boundary
            for (i = x1; i < x2; ++i) {
                temp = get_cost_2d(i, j, dir[dir_index], net_id, &distance);
                max_path.total_cost += max(static_cast<double>(0), temp);
                max_path.distance += distance;
                two_pin_L_path.path.push_back(&coor_array[i][j]);
                if (temp > max_path.max_cost)
                    max_path.max_cost = temp;
            }
            i = x2;
        } else // search in vertical direction
        {
            for (j = y1; (j < y2 && dir[dir_index] == FRONT) || (j > y2 && dir[dir_index] == BACK);) {
                temp = get_cost_2d(i, j, dir[dir_index], net_id, &distance);
                max_path.total_cost += max(static_cast<double>(0), temp);
                max_path.distance += distance;
                two_pin_L_path.path.push_back(&coor_array[i][j]);
                if (temp > max_path.max_cost)
                    max_path.max_cost = temp;
                if (dir[dir_index] == FRONT)
                    ++j;
                else if (dir[dir_index] == BACK)
                    --j;
            }
            j = y2;
        }
    }

    (*two_pin_L_path).path.push_back(&coor_array[x2][y2]);
    return max_path;
}

/*
 input: two coordinates
 output: record the L_pattern in the path, and the path is min max congestioned
 */
void Construct_2d_tree::L_pattern_route(int x1, int y1, int x2, int y2, Two_pin_element_2d& two_pin_L_path, int net_id) {
    Two_pin_element_2d path1, path2;
    Monotonic_element max_cong_path1, max_cong_path2;

    if (x1 > x2) {
        swap(x1, x2);
        swap(y1, y2);
    }

    if (x1 < x2 && y1 < y2)					//two points are left_back and right_front
            {
//FRONT and RIGHT L pattern from (x1 y1)
        max_cong_path1 = L_pattern_max_cong(x1, y1, x2, y2, FRONT, RIGHT, path1, net_id);
//RIGTH and FRONT L pattern from (x1,y1)
        max_cong_path2 = L_pattern_max_cong(x1, y1, x2, y2, RIGHT, FRONT, path2, net_id);
        if ((&max_cong_path1) == (compare_cost(&max_cong_path1, &max_cong_path2)))
            two_pin_L_path = path1;
        else
            two_pin_L_path = path2;
    } else if (x1 < x2 && y1 > y2)  //two points are left_front and right_back
            {
//BACK and RIGHT L pattern from (x1,y1)
        max_cong_path1 = L_pattern_max_cong(x1, y1, x2, y2, BACK, RIGHT, path1, net_id);
//RIGHT and BACK L pattern from (x1,y1)
        max_cong_path2 = L_pattern_max_cong(x1, y1, x2, y2, RIGHT, BACK, path2, net_id);
        if ((&max_cong_path1) == (compare_cost(&max_cong_path1, &max_cong_path2)))
            two_pin_L_path = path1;
        else
            two_pin_L_path = path2;
    } else // vertical or horizontal line
    {
        if (y1 > y2) {
            swap(y1, y2);
        }
        max_cong_path1 = L_pattern_max_cong(x1, y1, x2, y2, FRONT, RIGHT, two_pin_L_path, net_id);
    }
    (*two_pin_L_path).pin1 = *((*two_pin_L_path).path[0]);
    (*two_pin_L_path).pin2 = *((*two_pin_L_path).path.back());
    (*two_pin_L_path).net_id = net_id;
}

//Add the path of two pin element on to congestion map
//The congestion map record not only which net pass which edge,
//but also the number of a net pass through
void Construct_2d_tree::update_congestion_map_insert_two_pin_net(Two_pin_element_2d& element) {
    int dir;

    NetDirtyBit[element.net_id] = true;

    for (int i = element.path.size() - 2; i >= 0; --i) {
//get an edge from congestion map - c_map_2d
        dir = get_direction_2d(element.path[i], element.path[i + 1]);

        pair<RoutedNetTable::iterator, bool> insert_result = congestionMap2d.edge(element.path[i]->x, element.path[i]->y, dir).used_net.insert(pair<int, int>(element.net_id, 1));

        if (!insert_result.second)
            ++((insert_result.first)->second);
        else {
            ++(congestionMap2d.edge(element.path[i]->x, element.path[i]->y, dir).cur_cap);

            if (used_cost_flag != FASTROUTE_COST) {
                pre_evaluate_congestion_cost_fp(element.path[i]->x, element.path[i]->y, dir);
            }
        }
    }
}

//Remove a net from an edge.
//If the net pass that edge more than once, this function will only decrease the counter.
void Construct_2d_tree::update_congestion_map_remove_two_pin_net(Two_pin_element_2d& element) {
    int dir;

    NetDirtyBit[element.net_id] = true;

    for (int i = element.path.size() - 2; i >= 0; --i) {
        dir = get_direction_2d(element.path[i], element.path[i + 1]);

        RoutedNetTable::iterator find_result = congestionMap2d.edge(element.path[i]->x, element.path[i]->y, dir).used_net.find(element.net_id);

        --(find_result->second);
        if (find_result->second == 0) {
            congestionMap2d.edge(element.path[i]->x, element.path[i]->y, dir).used_net.erase(element.net_id);
            --(congestionMap2d.edge(element.path[i]->x, element.path[i]->y, dir).cur_cap);
            if (used_cost_flag != FASTROUTE_COST) {
                pre_evaluate_congestion_cost_fp(element.path[i]->x, element.path[i]->y, dir);
            }
        }
    }
}

void Construct_2d_tree::update_congestion_map_remove_multipin_net(Two_pin_list_2d& list) {
    for (Two_pin_element_2d& it : list) {
        update_congestion_map_remove_two_pin_net(it);
    }
}

//generate the congestion map by Flute with wirelength driven mode
void Construct_2d_tree::gen_FR_congestion_map() {
    std::vector<Tree> flutetree;                        //a struct, defined by Flute library
    Two_pin_element_2d *L_path;

    bboxRouteStateMap = EdgeColorMap<int>(rr_map.get_gridx(), rr_map.get_gridy(), -1);

    init_2d_map();          //initial congestion map: calculating every edge's capacity
    init_2pin_list();       //initial 2-pin net container
    init_flute();           //initial the information of pin's coordinate and group by net for flute
    flute_mode = NORMAL;	//wirelength driven	mode

    /*assign 0.5 demand to each net*/
#ifdef MESSAGE
    printf("bbox routing start...\n");
#endif	

//for storing the RSMT which returned by flute
    Flute netRoutingTreeRouter;
    flutetree.resize(rr_map.get_netNumber());

//Get every net's possible RSMT by flute, then use it to calculate the possible congestion
//In this section, we won't get a real routing result, but a possible congestion information.
    for (int i = 0; i < rr_map.get_netNumber(); ++i)	//i:net id
            {
#ifdef DEBUG_BBOX
        printf("bbox route net %d start...pin_num=%d\n",i,rr_map.get_netPinNumber(i));
#endif

//call flute to gen steiner tree and put the result in flutetree[]
        netRoutingTreeRouter.routeNet(rr_map.get_nPin(i), flutetree[i]);

//The total node # in a tree, those nodes include pin and steinor point
//And it is defined as ((2 * degree of a tree) - 2) by the authors of flute
        flutetree[i].number = 2 * flutetree[i].deg - 2;	//add 0403

        /*2-pin bounding box assign demand 0.5, remember not to repeat the same net*/
        for (int j = 0; j < flutetree[i].number; ++j)	//for all pins and steiner points
                {
            int x1 = (int) flutetree[i].branch[j].x;
            int y1 = (int) flutetree[i].branch[j].y;
            int x2 = (int) flutetree[i].branch[flutetree[i].branch[j].n].x;
            int y2 = (int) flutetree[i].branch[flutetree[i].branch[j].n].y;
            if (!(x1 == x2 && y1 == y2))	//start and end are not the same point
            {
                Two_pin_element_2d two_pin;
                two_pin.pin1.x = x1;
                two_pin.pin1.y = y1;
                two_pin.pin2.x = x2;
                two_pin.pin2.y = y2;
                two_pin.net_id = i;
                bbox_2pin_list[i].push_back(two_pin);
            }
        }

        bbox_route(bbox_2pin_list[i], 0.5);
    }
#ifdef DEBUG1
    printf("bbox routing complete\n");
    print_cap("max");
    print_cap("cur");
#endif	
#ifdef MESSAGE
    printf("L-shaped pattern routing start...\n");
#endif		

//sort net by their bounding box size, then by their pin number
    vector<const Net*> sort_net;
    for (int i = 0; i < rr_map.get_netNumber(); ++i) {
        sort_net.push_back(&rr_map.get_netList()[i]);
    }
    sort(sort_net.begin(), sort_net.end(), [&]( const Net* a, const Net* b ) {return comp_net(a,b);});

//Now begins the initial routing by pattern routing
//Edge shifting will also be applied to the routing.
    for (const Net* it : sort_net) {
        int netId = it->id;
        Tree& ftreeId = ftreeId;
        edge_shifting(&ftreeId);
        global_flutetree = ftreeId;

        std::vector<int> flute_order(2 * ftreeId.deg - 2);
        for (int i = global_flutetree.number - 1; i >= 0; --i) {
            flute_order[i] = i;
        }

        net_flutetree[netId] = ftreeId;

        /*remove demand*/
        bbox_route(bbox_2pin_list[netId], -0.5);

        for (int k = 0; k < ftreeId.number; ++k) {

            Branch& branch = ftreeId.branch[flute_order[k]];
            int x1 = (int) branch.x;
            int y1 = (int) branch.y;
            int x2 = (int) ftreeId.branch[branch.n].x;
            int y2 = (int) ftreeId.branch[branch.n].y;
            if (!(x1 == x2 && y1 == y2)) {
                /*choose the L-shape with lower congestion to assign new demand 1*/
                Two_pin_element_2d L_path;
                L_pattern_route(x1, y1, x2, y2, L_path, netId);

                /*insert 2pin_path into this net*/
                net_2pin_list[netId].push_back(L_path);
                update_congestion_map_insert_two_pin_net(L_path);
#ifdef DEBUG_LROUTE
                print_path(*L_path);
#endif
            }
        }
#ifdef FREE
        free(flute_order);
#endif
    }

#ifdef MESSAGE
    printf("generate L-shape congestion map in stage1 successfully\n");
#endif	

#ifdef DEBUG1
    print_cap("cur");
#endif
    cal_max_overflow();
    delete bboxRouteStateMap;
}

//=====================edge shifting=============================
//Return the smaller cost of L-shape path or the only one cost of flap path
double Construct_2d_tree::compute_L_pattern_cost(int x1, int y1, int x2, int y2, int net_id) {
    Two_pin_element_2d path1, path2;
    Monotonic_element max_cong_path1, max_cong_path2;

    if (x1 > x2) {
        swap(x1, x2);
        swap(y1, y2);
    }

    if (x1 < x2 && y1 < y2) {
        max_cong_path1 = L_pattern_max_cong(x1, y1, x2, y2, FRONT, RIGHT, &path1, net_id);
        max_cong_path2 = L_pattern_max_cong(x1, y1, x2, y2, RIGHT, FRONT, &path2, net_id);

        if ((&max_cong_path1) == (compare_cost(&max_cong_path1, &max_cong_path2)))
            return max_cong_path1.total_cost;
        else
            return max_cong_path2.total_cost;
    } else if (x1 < x2 && y1 > y2) {
        max_cong_path1 = L_pattern_max_cong(x1, y1, x2, y2, BACK, RIGHT, &path1, net_id);
        max_cong_path2 = L_pattern_max_cong(x1, y1, x2, y2, RIGHT, BACK, &path2, net_id);

        if ((&max_cong_path1) == (compare_cost(&max_cong_path1, &max_cong_path2)))
            return max_cong_path1.total_cost;
        else
            return max_cong_path2.total_cost;
    } else // vertical or horizontal line
    {
        if (y1 > y2) {
            swap(y1, y2);
        }
        max_cong_path1 = L_pattern_max_cong(x1, y1, x2, y2, FRONT, RIGHT, &path1, net_id);
        return max_cong_path1.total_cost;
    }
}

void Construct_2d_tree::find_saferange(Vertex_flute_ptr a, Vertex_flute_ptr b, int *low, int *high, int dir) {
    Vertex_flute_ptr cur, find;

//Horizontal edge doing vertical shifting
    if (dir == HOR) {
        cur = a;
        while (cur->type != PIN) {
            find = *(cur->neighbor.begin());
            for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei) {
                if ((*nei)->y > find->y) {
                    find = *nei;
                }
            }
            cur = find;
            if ((cur->y == a->y) || (cur->x != a->x))
                break;
        }
        *high = min(*high, cur->y);

        cur = b;
        while (cur->type != PIN) {
            find = *(cur->neighbor.begin());
            for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                if ((*nei)->y > find->y)
                    find = *nei;
            cur = find;
            if ((cur->y == b->y) || (cur->x != b->x))
                break;
        }
        *high = min(*high, cur->y);

        cur = a;
        while (cur->type != PIN) {
            find = *(cur->neighbor.begin());
            for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                if ((*nei)->y < find->y)
                    find = *nei;
            cur = find;
            if (cur->y == a->y || cur->x != a->x)
                break;
        }
        *low = max(*low, cur->y);

        cur = b;
        while (cur->type != PIN) {
            find = *(cur->neighbor.begin());
            for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                if ((*nei)->y < find->y)
                    find = *nei;
            cur = find;
            if ((cur->y == b->y) || (cur->x != b->x))
                break;
        }
        *low = max(*low, cur->y);
    } else {
        cur = a;
        while (cur->type != PIN) {
            find = *(cur->neighbor.begin());
            for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                if ((*nei)->x > find->x)
                    find = *nei;
            cur = find;
            if (cur->x == a->x || cur->y != a->y)	//no neighboring vertex in the right of a
                break;
        }
        *high = min(*high, cur->x);
        cur = b;
        while (cur->type != PIN) {
            find = *(cur->neighbor.begin());
            for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                if ((*nei)->x > find->x)
                    find = *nei;
            cur = find;
            if (cur->x == b->x || cur->y != b->y)	//no neighboring vertex in the right of b
                break;
        }
        *high = min(*high, cur->x);
        cur = a;
        while (cur->type != PIN) {
            find = *(cur->neighbor.begin());
            for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                if ((*nei)->x < find->x)
                    find = *nei;
            cur = find;
            if (cur->x == a->x || cur->y != a->y)	//no neighboring vertex in the left of a
                break;
        }
        *low = max(*low, cur->x);
        cur = b;
        while (cur->type != PIN) {
            find = *(cur->neighbor.begin());
            for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                if ((*nei)->x < find->x)
                    find = *nei;
            cur = find;
            if (cur->x == b->x || cur->y != b->y)	//no neighboring vertex in the left of b
                break;
        }
        *low = max(*low, cur->x);
    }
}

void Construct_2d_tree::merge_vertex(Vertex_flute_ptr keep, Vertex_flute_ptr deleted) {

    for (vector<Vertex_flute_ptr>::iterator nei = deleted->neighbor.begin(); nei != deleted->neighbor.end(); ++nei) {
        if ((*nei) != keep)	//nei is not keep itself
                {
            keep->neighbor.push_back(*nei);	//add deleted's neighbor to keep
            for (int find = 0;; ++find) {
//#ifdef DEBUG_EDGESHIFT
                if (find == (int) (*nei)->neighbor.size()) {
                    printf("wrong in merge_vertex\n");
                    exit(0);
                }
//#endif
                if ((*nei)->neighbor[find]->x == deleted->x && (*nei)->neighbor[find]->y == deleted->y)	//neighbor[find] equals to deleted
                        {
                    (*nei)->neighbor[find] = keep;	//replace its neighbor as keep
                    break;
                }
            }
        }
    }
    deleted->type = DELETED;
}

bool Construct_2d_tree::move_edge(Vertex_flute_ptr a, Vertex_flute_ptr b, int best_pos, int dir) {
    vector<Vertex_flute_ptr> st_pt;
    Vertex_flute_ptr cur, find;
    Vertex_flute_ptr overlap_a, overlap_b;
    int ind1, ind2;

    overlap_a = overlap_b = NULL;
    if (dir == HOR) {
        if (best_pos > a->y)	//move up
                {
            //find all steiner points between a and best_pos
            cur = a;
            while (cur->y < best_pos) {
                find = *(cur->neighbor.begin());
                for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                    if ((*nei)->y > find->y)
                        find = *nei;
                cur = find;
                st_pt.push_back(cur);
            }
            //exchange neighb
            for (int i = 0; i < (int) st_pt.size(); ++i) {
                if (st_pt[i]->y < best_pos) {
                    for (ind1 = 0;; ++ind1) {
                        if (!(((a->neighbor[ind1]->x == b->x) && (a->neighbor[ind1]->y == b->y)) || ((a->neighbor[ind1]->x == st_pt[i]->x) && (a->neighbor[ind1]->y == st_pt[i]->y)))) {
                            break;
                        }
                    }
                    for (ind2 = 0;; ++ind2)
                        if (st_pt[i]->neighbor[ind2]->y > st_pt[i]->y)
                            break;
                    for (int j = 0;; ++j)
                        if (a->neighbor[ind1]->neighbor[j] == a) {
                            a->neighbor[ind1]->neighbor[j] = st_pt[i];
                            break;
                        }
                    for (int j = 0;; ++j)
                        if (st_pt[i]->neighbor[ind2]->neighbor[j] == st_pt[i]) {
                            st_pt[i]->neighbor[ind2]->neighbor[j] = a;
                            break;
                        }

                    swap(a->neighbor[ind1], st_pt[i]->neighbor[ind2]);

                    a->y = st_pt[i]->y;
                } else if (st_pt[i]->x == a->x && st_pt[i]->y == best_pos)
                    overlap_a = st_pt[i];
                else
                    break;
            }
            st_pt.clear();
            cur = b;
            while (cur->y < best_pos) {
                find = *(cur->neighbor.begin());
                for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                    if ((*nei)->y > find->y)
                        find = *nei;
                cur = find;
                st_pt.push_back(cur);
            }
            for (int i = 0; i < (int) st_pt.size(); ++i) {
                if (st_pt[i]->y < best_pos) {
                    for (ind1 = 0;; ++ind1)
                        if (!((b->neighbor[ind1]->x == a->x && b->neighbor[ind1]->y == a->y) || (b->neighbor[ind1]->x == st_pt[i]->x && b->neighbor[ind1]->y == st_pt[i]->y)))
                            break;
                    for (ind2 = 0;; ++ind2)
                        if (st_pt[i]->neighbor[ind2]->y > st_pt[i]->y)
                            break;

                    for (int j = 0;; ++j)
                        if (b->neighbor[ind1]->neighbor[j] == b) {
                            b->neighbor[ind1]->neighbor[j] = st_pt[i];
                            break;
                        }
                    for (int j = 0;; ++j)
                        if (st_pt[i]->neighbor[ind2]->neighbor[j] == st_pt[i]) {
                            st_pt[i]->neighbor[ind2]->neighbor[j] = b;
                            break;
                        }

                    swap(b->neighbor[ind1], st_pt[i]->neighbor[ind2]);

                    b->y = st_pt[i]->y;
                } else if (st_pt[i]->x == b->x && st_pt[i]->y == best_pos)
                    overlap_b = st_pt[i];
                else
                    break;
            }
            st_pt.clear();

            a->y = best_pos;
            b->y = best_pos;
        } else	//move down
        {
            cur = a;
            while (cur->y > best_pos) {
                find = *(cur->neighbor.begin());
                for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                    if ((*nei)->y < find->y)
                        find = *nei;
                cur = find;
                st_pt.push_back(cur);
            }
            for (int i = 0; i < (int) st_pt.size(); ++i) {
                if (st_pt[i]->y > best_pos) {
                    for (ind1 = 0;; ++ind1)
                        if (!((a->neighbor[ind1]->x == b->x && a->neighbor[ind1]->y == b->y) || (a->neighbor[ind1]->x == st_pt[i]->x && a->neighbor[ind1]->y == st_pt[i]->y)))
                            break;
                    for (ind2 = 0;; ++ind2)
                        if (st_pt[i]->neighbor[ind2]->y < st_pt[i]->y)
                            break;
                    for (int j = 0;; ++j)
                        if (a->neighbor[ind1]->neighbor[j] == a) {
                            a->neighbor[ind1]->neighbor[j] = st_pt[i];
                            break;
                        }
                    for (int j = 0;; ++j)
                        if (st_pt[i]->neighbor[ind2]->neighbor[j] == st_pt[i]) {
                            st_pt[i]->neighbor[ind2]->neighbor[j] = a;
                            break;
                        }

                    swap(a->neighbor[ind1], st_pt[i]->neighbor[ind2]);

                    a->y = st_pt[i]->y;
                } else if (st_pt[i]->x == a->x && st_pt[i]->y == best_pos)
                    overlap_a = st_pt[i];
                else
                    break;
            }
            st_pt.clear();
            cur = b;
            while (cur->y > best_pos) {
                find = *(cur->neighbor.begin());
                for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                    if ((*nei)->y < find->y)
                        find = *nei;
                cur = find;
                st_pt.push_back(cur);
            }
            for (int i = 0; i < (int) st_pt.size(); ++i) {
                if (st_pt[i]->y > best_pos) {
                    for (ind1 = 0;; ++ind1)
                        if (!((b->neighbor[ind1]->x == a->x && b->neighbor[ind1]->y == a->y) || (b->neighbor[ind1]->x == st_pt[i]->x && b->neighbor[ind1]->y == st_pt[i]->y)))
                            break;
                    for (ind2 = 0;; ++ind2)
                        if (st_pt[i]->neighbor[ind2]->y < st_pt[i]->y)
                            break;

                    for (int j = 0;; ++j)
                        if (b->neighbor[ind1]->neighbor[j] == b) {
                            b->neighbor[ind1]->neighbor[j] = st_pt[i];
                            break;
                        }
                    for (int j = 0;; ++j)
                        if (st_pt[i]->neighbor[ind2]->neighbor[j] == st_pt[i]) {
                            st_pt[i]->neighbor[ind2]->neighbor[j] = b;
                            break;
                        }

                    swap(b->neighbor[ind1], st_pt[i]->neighbor[ind2]);

                    b->y = st_pt[i]->y;
                } else if (st_pt[i]->x == b->x && st_pt[i]->y == best_pos)
                    overlap_b = st_pt[i];
                else
                    break;
            }
            st_pt.clear();

            a->y = best_pos;
            b->y = best_pos;
        }
    } else	//VER
    {
        if (best_pos > a->x)	//move right
                {
            //find all steiner points between a and best_pos
            cur = a;
            while (cur->x < best_pos) {
                find = *(cur->neighbor.begin());
                for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                    if ((*nei)->x > find->x)
                        find = *nei;
                cur = find;
                st_pt.push_back(cur);
            }
            //exchange neighbor
            for (int i = 0; i < (int) st_pt.size(); ++i) {
                if (st_pt[i]->x < best_pos) {
                    for (ind1 = 0;; ++ind1)
                        if (!((a->neighbor[ind1]->x == b->x && a->neighbor[ind1]->y == b->y) || (a->neighbor[ind1]->x == st_pt[i]->x && a->neighbor[ind1]->y == st_pt[i]->y)))
                            break;
                    for (ind2 = 0;; ++ind2)
                        if (st_pt[i]->neighbor[ind2]->x > st_pt[i]->x)
                            break;

                    for (int j = 0;; ++j)
                        if (a->neighbor[ind1]->neighbor[j] == a) {
                            a->neighbor[ind1]->neighbor[j] = st_pt[i];
                            break;
                        }
                    for (int j = 0;; ++j)
                        if (st_pt[i]->neighbor[ind2]->neighbor[j] == st_pt[i]) {
                            st_pt[i]->neighbor[ind2]->neighbor[j] = a;
                            break;
                        }

                    swap(a->neighbor[ind1], st_pt[i]->neighbor[ind2]);

                    a->x = st_pt[i]->x;
                } else if (st_pt[i]->x == best_pos && st_pt[i]->y == a->y)
                    overlap_a = st_pt[i];
                else
                    break;
            }
            st_pt.clear();
            cur = b;
            while (cur->x < best_pos) {
                find = *(cur->neighbor.begin());
                for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                    if ((*nei)->x > find->x)
                        find = *nei;
                cur = find;
                st_pt.push_back(cur);
            }
            //exchange neighbor
            for (int i = 0; i < (int) st_pt.size(); ++i) {
                if (st_pt[i]->x < best_pos) {
                    for (ind1 = 0;; ++ind1)
                        if (!((b->neighbor[ind1]->x == a->x && b->neighbor[ind1]->y == a->y) || (b->neighbor[ind1]->x == st_pt[i]->x && b->neighbor[ind1]->y == st_pt[i]->y)))
                            break;
                    for (ind2 = 0;; ++ind2)
                        if (st_pt[i]->neighbor[ind2]->x > st_pt[i]->x)
                            break;

                    for (int j = 0;; ++j)
                        if (b->neighbor[ind1]->neighbor[j] == b) {
                            b->neighbor[ind1]->neighbor[j] = st_pt[i];
                            break;
                        }
                    for (int j = 0;; ++j)
                        if (st_pt[i]->neighbor[ind2]->neighbor[j] == st_pt[i]) {
                            st_pt[i]->neighbor[ind2]->neighbor[j] = b;
                            break;
                        }

                    swap(b->neighbor[ind1], st_pt[i]->neighbor[ind2]);

                    b->x = st_pt[i]->x;
                } else if (st_pt[i]->x == best_pos && st_pt[i]->y == b->y)
                    overlap_b = st_pt[i];
                else
                    break;
            }
            st_pt.clear();

            a->x = best_pos;
            b->x = best_pos;
        } else	//move left
        {
            cur = a;
            while (cur->x > best_pos) {
                find = *(cur->neighbor.begin());
                for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                    if ((*nei)->x < find->x)
                        find = *nei;
                cur = find;
                st_pt.push_back(cur);
            }
            for (int i = 0; i < (int) st_pt.size(); ++i) {
                if (st_pt[i]->x > best_pos) {
                    for (ind1 = 0;; ++ind1)
                        if (!((a->neighbor[ind1]->x == b->x && a->neighbor[ind1]->y == b->y) || (a->neighbor[ind1]->x == st_pt[i]->x && a->neighbor[ind1]->y == st_pt[i]->y)))
                            break;
                    for (ind2 = 0;; ++ind2)
                        if (st_pt[i]->neighbor[ind2]->x < st_pt[i]->x)
                            break;

                    for (int j = 0;; ++j)
                        if (a->neighbor[ind1]->neighbor[j] == a) {
                            a->neighbor[ind1]->neighbor[j] = st_pt[i];
                            break;
                        }
                    for (int j = 0;; ++j)
                        if (st_pt[i]->neighbor[ind2]->neighbor[j] == st_pt[i]) {
                            st_pt[i]->neighbor[ind2]->neighbor[j] = a;
                            break;
                        }

                    swap(a->neighbor[ind1], st_pt[i]->neighbor[ind2]);

                    a->x = st_pt[i]->x;
                } else if (st_pt[i]->x == best_pos && st_pt[i]->y == a->y)
                    overlap_a = st_pt[i];
                else
                    break;
            }
            st_pt.clear();
            cur = b;
            while (cur->x > best_pos) {
                find = *(cur->neighbor.begin());
                for (vector<Vertex_flute_ptr>::iterator nei = cur->neighbor.begin() + 1; nei != cur->neighbor.end(); ++nei)
                    if ((*nei)->x < find->x)
                        find = *nei;
                cur = find;
                st_pt.push_back(cur);
            }
            for (int i = 0; i < (int) st_pt.size(); ++i) {
                if (st_pt[i]->x > best_pos) {
                    for (ind1 = 0;; ++ind1)
                        if (!((b->neighbor[ind1]->x == a->x && b->neighbor[ind1]->y == a->y) || (b->neighbor[ind1]->x == st_pt[i]->x && b->neighbor[ind1]->y == st_pt[i]->y)))
                            break;
                    for (ind2 = 0;; ++ind2)
                        if (st_pt[i]->neighbor[ind2]->x < st_pt[i]->x)
                            break;

                    for (int j = 0;; ++j)
                        if (b->neighbor[ind1]->neighbor[j] == b) {
                            b->neighbor[ind1]->neighbor[j] = st_pt[i];
                            break;
                        }
                    for (int j = 0;; ++j)
                        if (st_pt[i]->neighbor[ind2]->neighbor[j] == st_pt[i]) {
                            st_pt[i]->neighbor[ind2]->neighbor[j] = b;
                            break;
                        }

                    swap(b->neighbor[ind1], st_pt[i]->neighbor[ind2]);

                    b->x = st_pt[i]->x;
                } else if (st_pt[i]->x == best_pos && st_pt[i]->y == b->y)
                    overlap_b = st_pt[i];
                else
                    break;
            }
            st_pt.clear();

            a->x = best_pos;
            b->x = best_pos;
        }
    }

    bool ret = false;
    if (overlap_a != NULL) {
        merge_vertex(overlap_a, a);
        ret = true;
    }
    if (overlap_b != NULL)
        merge_vertex(overlap_b, b);

    return ret;
}

void Construct_2d_tree::traverse_tree(double *ori_cost) {
    double cur_cost, tmp_cost, best_cost;
    int best_pos = 0;
    Vertex_flute_ptr node;

    for (vector<Vertex_flute_ptr>::iterator it_v = vertex_fl.begin(); it_v != vertex_fl.end(); ++it_v) {
        node = *it_v;
        node->visit = 1;

        if (node->type == STEINER && node->neighbor.size() <= 3)	//remove3!!!
                {
            for (vector<Vertex_flute_ptr>::iterator it = node->neighbor.begin(); it != node->neighbor.end(); ++it)
                if ((*it)->visit == 0 && ((*it)->type == STEINER && (*it)->neighbor.size() <= 3))	//!!!remove3!!
                        {
                    int low = 0, high = INT_MAX;
                    Vertex_flute_ptr a = node, b = (*it);

                    if ((*it)->y == node->y)	//horizontal edge (vertical shifting)
                            {
                        find_saferange(a, b, &low, &high, HOR);		//compute safe range
                        best_cost = *ori_cost;
                        cur_cost = (*ori_cost) - compute_L_pattern_cost(a->x, a->y, b->x, b->y, -1);
                        for (int pos = low; pos <= high; ++pos) {
                            tmp_cost = cur_cost + compute_L_pattern_cost(a->x, pos, b->x, pos, -1);
                            if (tmp_cost < best_cost) {
                                best_cost = tmp_cost;
                                best_pos = pos;
                            }
                        }
                        if (best_cost < *ori_cost)	//edge need shifting
                                {
                            move_edge(a, b, best_pos, HOR);
                            //move to best position,exchange steiner points if needed
                            *ori_cost = best_cost;
                        }
                    } else if ((*it)->x == node->x)	//vertical edge (horizontal shifting)
                            {
                        find_saferange(a, b, &low, &high, VER);		//compute safe range
                        best_cost = *ori_cost;
                        cur_cost = (*ori_cost) - compute_L_pattern_cost(a->x, a->y, b->x, b->y, -1);
                        for (int pos = low; pos <= high; ++pos) {
                            tmp_cost = cur_cost + compute_L_pattern_cost(pos, a->y, pos, b->y, -1);
                            if (tmp_cost < best_cost) {
                                best_cost = tmp_cost;
                                best_pos = pos;
                            }
                        }
                        if (best_cost < *ori_cost)	//edge need shifting
                                {
                            move_edge(a, b, best_pos, VER);
                            //move to best position,exchange steiner points if needed
                            *ori_cost = best_cost;
                        }
                    } else
                        continue;
                }
        }
    }
}

void Construct_2d_tree::dfs_output_tree(Vertex_flute_ptr node, Tree *t) {
    node->visit = 1;
    t->branch[t->number].x = node->x;
    t->branch[t->number].y = node->y;
    node->index = t->number;
    (t->number) += 1;
    for (vector<Vertex_flute_ptr>::iterator it = node->neighbor.begin(); it != node->neighbor.end(); ++it) {
        if (((*it)->visit == 0) && ((*it)->type != DELETED)) {
            dfs_output_tree((*it), t);                  //keep tracing deeper vertice
            t->branch[(*it)->index].n = node->index;    //make parsent of target vertex point to current vertex
        }
    }
}

void Construct_2d_tree::edge_shifting(Tree *t) {
    Vertex_flute_ptr new_v;
    double ori_cost;            // the original cost without edge shifting

    ori_cost = 0;
//creat vertex
    for (int i = 0; i < t->deg; ++i) {
        new_v = new Vertex_flute((int) t->branch[i].x, (int) t->branch[i].y);
        new_v->type = PIN;
        vertex_fl.push_back(new_v);
    }
    for (int i = t->deg; i < 2 * t->deg - 2; ++i) {
        new_v = new Vertex_flute((int) t->branch[i].x, (int) t->branch[i].y);
        new_v->type = STEINER;
        vertex_fl.push_back(new_v);
    }

//creat edge
    for (int i = 0; i < 2 * (t->deg) - 2; ++i) {
//skip the vertex if it is the same vertex with its neighbor
        if ((vertex_fl[i]->x == vertex_fl[t->branch[i].n]->x) && (vertex_fl[i]->y == vertex_fl[t->branch[i].n]->y))
            continue;
        vertex_fl[i]->neighbor.push_back(vertex_fl[t->branch[i].n]);
        vertex_fl[t->branch[i].n]->neighbor.push_back(vertex_fl[i]);
//compute original tree cost
        ori_cost += compute_L_pattern_cost(vertex_fl[i]->x, vertex_fl[i]->y, vertex_fl[t->branch[i].n]->x, vertex_fl[t->branch[i].n]->y, -1);
    }
    std::sort(vertex_fl.begin(), vertex_fl.end(), [&](Vertex_flute_ptr a, Vertex_flute_ptr b) {return comp_vertex_fl(a,b);});

    for (int i = 0, j = 1; j < 2 * (t->deg) - 2; ++j) {
        if ((vertex_fl[i]->x == vertex_fl[j]->x) && (vertex_fl[i]->y == vertex_fl[j]->y))	//j is redundant
                {
            vertex_fl[j]->type = DELETED;
            for (vector<Vertex_flute_ptr>::iterator it = vertex_fl[j]->neighbor.begin(); it != vertex_fl[j]->neighbor.end(); ++it) {
                if (((*it)->x != vertex_fl[i]->x) || ((*it)->y != vertex_fl[i]->y))	//not i,add 0430
                        {
                    vertex_fl[i]->neighbor.push_back(*it);
                    for (int k = 0; k < (int) (*it)->neighbor.size(); k++) {
                        if ((*it)->neighbor[k]->x == vertex_fl[i]->x && (*it)->neighbor[k]->y == vertex_fl[i]->y) {
                            (*it)->neighbor[k] = vertex_fl[i];	//modify (*it)'s neighbor to i
                            break;
                        }
                    }
                }
            }
        } else
            i = j;
    }

    for (int i = 0; i < 2 * t->deg - 2; ++i)
        vertex_fl[i]->visit = 0;
    traverse_tree(&ori_cost);	// dfs to find 2 adjacent steiner points(h or v edge) and do edge_shifhting

//Output the result (2-pin lists) to a Tree structure in DFS order
//1. make sure every 2-pin list have not been visited
    for (int i = 0; i < 2 * t->deg - 2; ++i) {
        vertex_fl[i]->visit = 0;
    }
    t->number = 0;

//2. begin to out put the 2-pin lists to a Tree strucuture
    dfs_output_tree(vertex_fl[0], t);
    t->branch[0].n = 0;	//because neighbor of root is not assign in dfs_output_tree()

//3. free memory resources
    for (int i = 0; i < (int) vertex_fl.size(); ++i)
        delete (vertex_fl[i]);
    vertex_fl.clear();
}
//=====================end edge shifting=============================

/* sort by bounding box size */
void Construct_2d_tree::output_2_pin_list() {
    std::sort(all_two_pin_list.begin(), all_two_pin_list.end(), [&](Two_pin_element *a, Two_pin_element *b) {
        return comp_2pin_net(a,b);
    });
    std::sort(two_pin_list.begin(), two_pin_list.end(), [&](Two_pin_element_2d *a, Two_pin_element_2d *b) {
        return comp_2pin_net_from_path(a,b);
    });
}



/*stage 1: contruct 2d steiner tree
 output 2-pin list to stage2
 return max_overflow;
 */

Construct_2d_tree::Construct_2d_tree(RoutingParameters& routingparam, ParameterSet& param, RoutingRegion& rr) :
//
        parameter_set { param }, //
        routing_parameter { routingparam }, //
        dir_array { { { 0, 1 }, { 0, -1 }, { -1, 0 }, { 1, 0 } } }, ////FRONT,BACK,LEFT,RIGHT

        rr_map { rr }, //
        post_processing { *this } {

    par_ind = 0;

    mazeroute_in_range = NULL;

    max_congestion_factor = 1.0;

    /***********************
     * Global Variable End
     * ********************/

    adjust_value = 0;

    cur_iter = -1;                  // current iteration ID.
//edgeIterCounter = new EdgeColorMap<int>(rr_map.get_gridx(), rr_map.get_gridy(), -1);

    total_overflow = 0;             // used by post-processing

    readLUT();                      // Function in flute, function: unknown

    /* TroyLee: NetDirtyBit Counter */
    NetDirtyBit = vector<bool>(rr_map.get_netNumber(), true);
    /* TroyLee: End */

    allocate_coor_array();          // Make an 2D coordinate array which contains the (x, y) information

    if (routing_parameter.get_monotonic_en()) {
        //allocate_monotonic();           // Allocate the memory for storing the data while searching monotonic path
// 1. A 2D array that stores max congestion
// 2. A 2D array that stores parent (x,y) during finding monotonic path
    }

    gen_FR_congestion_map();        // Generate congestion map by flute, then route all nets by L-shap pattern routing with
// congestion information from this map. After that, apply edge shifting to the result
// to get the initial solution.

    cal_total_wirelength();         // The report value is the sum of demand on every edge

    RangeRouter rangeRouter(*this);

    Route_2pinnets route_2pinnets(*this, rangeRouter);

    route_2pinnets.allocate_gridcell();            //question: I don't know what this for. (jalamorm, 07/10/31)

//Make a 2-pin net list without group by net
    for (int i = 0; i < rr_map.get_netNumber(); ++i) {
        for (int j = 0; j < (int) net_2pin_list[i]->size(); ++j) {
            two_pin_list.push_back((*net_2pin_list[i])[j]);
        }
    }

    route_2pinnets.reallocate_two_pin_list(true);
    cache = new EdgePlane<CacheEdge>(rr_map.get_gridx(), rr_map.get_gridy(), CacheEdge());
    mazeroute_in_range = new Multisource_multisink_mazeroute(*this);

    int cur_overflow = -1;
    used_cost_flag = HISTORY_COST;
    BOXSIZE_INC = routing_parameter.get_init_box_size_p2();

    for (cur_iter = 1, done_iter = cur_iter; cur_iter <= routing_parameter.get_iteration_p2(); ++cur_iter, done_iter = cur_iter)   //do n-1 times
            {
        cout << "\033[31mIteration:\033[m " << cur_iter << endl;

        factor = (1.0 - exp(-5 * exp(-(0.1 * cur_iter))));

        WL_Cost = factor;
        via_cost = static_cast<int>(4 * factor);
        adjust_value = cur_iter * (1.25 + 3 * factor); //tuned for experimant

#ifdef MESSAGE
        cout << "Parameters - Factor: " << factor
        << ", Via_Cost: " << via_cost
        << ", Box Size: " << BOXSIZE_INC + cur_iter - 1 << endl;
#endif

        pre_evaluate_congestion_cost();

//route_all_2pin_net(false);
        route_2pinnets.route_all_2pin_net();

        cur_overflow = cal_max_overflow();
        cal_total_wirelength();

        if (cur_overflow == 0)
            break;

        route_2pinnets.reallocate_two_pin_list(true);

#ifdef MESSAGE
        printMemoryUsage("Memory Usage:");
#endif

        if (cur_overflow <= routing_parameter.get_overflow_threshold()) {
            break;
        }
        BOXSIZE_INC += routing_parameter.get_box_size_inc_p2();
    }

    output_2_pin_list();    //order:bbox�p

#ifdef FREE
    free_memory_con2d();
#endif
#ifdef MESSAGE
    cout<<"================================================================"<<endl;
    cout<<"===                   Enter Post Processing                  ==="<<endl;
    cout<<"================================================================"<<endl;
#endif
    post_processing.process(route_2pinnets);
}

inline void Construct_2d_tree::printMemoryUsage(const char* msg) {
    std::cout << msg << std::endl;
    //for print out memory usage
    std::ifstream mem("/proc/self/status");
    std::string memory;
    for (unsigned i = 0; i < 13; ++i) {
        getline(mem, memory);
        if (i > 10) {
            std::cout << memory << std::endl;
        }
    }
}
