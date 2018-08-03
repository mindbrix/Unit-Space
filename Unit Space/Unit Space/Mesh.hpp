//
//  Mesh.hpp
//  Unit Space
//
//  Created by Nigel Barber on 30/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <vector>
using namespace std;

#pragma clang diagnostic ignored "-Wcomma"

struct Mesh {
    vector<MeshPath>                    _paths;
    vector<Vector>                      _pts;
    vector<Bounds>                      _bounds;
        
    void empty() { _paths.resize(0), _pts.resize(0), _bounds.resize(0); }
};
