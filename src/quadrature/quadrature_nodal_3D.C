// The libMesh Finite Element Library.
// Copyright (C) 2002-2021 Benjamin S. Kirk, John W. Peterson, Roy H. Stogner

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA



// Local includes
#include "libmesh/quadrature_nodal.h"
#include "libmesh/quadrature_trap.h"
#include "libmesh/quadrature_simpson.h"
#include "libmesh/enum_to_string.h"

namespace libMesh
{

void QNodal::init_3D(const ElemType, unsigned int)
{
#if LIBMESH_DIM == 3

  switch (_type)
    {
    case TET4:
    case PRISM6:
    case HEX8:
    case PYRAMID5:
      {
        QTrap rule(/*dim=*/3, /*ignored*/_order);
        rule.init(_type, /*ignored*/_p_level);
        _points.swap (rule.get_points());
        _weights.swap(rule.get_weights());
        return;
      }

    case PRISM15:
      {
        // A rule with 15 points which is exact for linears, and
        // naturally produces a lumped approximation to the mass
        // matrix. The quadrature points are numbered the same way as
        // the reference element nodes.
        _points =
          {
            Point(0.,0.,-1), Point(+1,0.,-1), Point(0.,+1,-1),
            Point(0.,0.,+1), Point(+1,0.,+1), Point(0.,+1,+1),
            Point(.5,0.,-1), Point(.5,.5,-1), Point(0.,.5,-1),
            Point(0.,0.,0.), Point(+1,0.,0.), Point(0.,+1,0.),
            Point(.5,0.,+1), Point(.5,.5,+1), Point(0.,.5,+1),
          };

        // vertex (wv), tri edge (wt), and quad edge (wq) weights are
        // obtained using the same approach that was used for the Quad8,
        // see quadrature_nodal_2D.C for details.
        Real wv = Real(1) / 34;
        Real wt = Real(4) / 51;
        Real wq = Real(2) / 17;

        _weights = {wv, wv, wv, wv, wv, wv,
                    wt, wt, wt,
                    wq, wq, wq,
                    wt, wt, wt};

        return;
      }

    case HEX20:
      {
        // A rule with 20 points which is exact for linears, and
        // naturally produces a lumped approximation to the mass
        // matrix. The quadrature points are numbered the same way as
        // the reference element nodes.
        _points =
          {
            Point(-1,-1,-1), Point(+1,-1,-1), Point(+1,+1,-1), Point(-1,+1,-1),
            Point(-1,-1,+1), Point(+1,-1,+1), Point(+1,+1,+1), Point(-1,+1,+1),
            Point(0.,-1,-1), Point(+1,0.,-1), Point(0.,+1,-1), Point(-1,0.,-1),
            Point(-1,-1,0.), Point(+1,-1,0.), Point(+1,+1,0.), Point(-1,+1,0.),
            Point(0.,-1,+1), Point(+1,0.,+1), Point(0.,+1,+1), Point(-1,0.,+1)
          };

        // vertex (wv), and edge (we) weights are obtained using the
        // same approach that was used for the Quad8, see
        // quadrature_nodal_2D.C for details.
        Real wv = Real(7) / 31;
        Real we = Real(16) / 31;

        _weights = {wv, wv, wv, wv, wv, wv, wv, wv,
                    we, we, we, we, we, we, we, we, we, we, we, we};

        return;
      }

    case TET10:
    case PRISM18:
    case HEX27:
    case PYRAMID13:
    case PYRAMID14:
      {
        QSimpson rule(/*dim=*/3, /*ignored*/_order);
        rule.init(_type, /*ignored*/_p_level);
        _points.swap (rule.get_points());
        _weights.swap(rule.get_weights());

        // We can't do a proper Simpson rule for pyramids regardless
        if (_type == PYRAMID13)
          {
            _points.resize(13);
            _weights.resize(13);
          }

        return;
      }

    default:
      libmesh_error_msg("ERROR: Unsupported type: " << Utility::enum_to_string(_type));
    }
#endif
}

} // namespace libMesh
