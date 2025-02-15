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
#include "libmesh/elem.h"
#include "libmesh/fe.h"
#include "libmesh/fe_interface.h"
#include "libmesh/fe_macro.h"
#include "libmesh/libmesh_logging.h"
#include "libmesh/quadrature.h"
#include "libmesh/tensor_value.h"
#include "libmesh/enum_elem_type.h"
#include "libmesh/quadrature_gauss.h"

namespace {
  // Put this outside a templated class, so we only get 1 warning
  // during our unit tests, not 1 warning for each of the zillion FE
  // specializations we test.
  void nonlagrange_dual_warning () {
    libmesh_warning("dual calculations have only been verified for the LAGRANGE family");
  }
}


namespace libMesh
{


// ------------------------------------------------------------
// FE class members
template <unsigned int Dim, FEFamily T>
FE<Dim,T>::FE (const FEType & fet) :
  FEGenericBase<typename FEOutputType<T>::type> (Dim,fet),
  last_side(INVALID_ELEM),
  last_edge(libMesh::invalid_uint)
{
  // Sanity check.  Make sure the
  // Family specified in the template instantiation
  // matches the one in the FEType object
  libmesh_assert_equal_to (T, this->get_family());
}


template <unsigned int Dim, FEFamily T>
unsigned int FE<Dim,T>::n_shape_functions () const
{
  return FE<Dim,T>::n_dofs (this->elem_type,
                            static_cast<Order>(this->fe_type.order + this->_p_level));
}


template <unsigned int Dim, FEFamily T>
void FE<Dim,T>::attach_quadrature_rule (QBase * q)
{
  libmesh_assert(q);
  this->qrule = q;
  // make sure we don't cache results from a previous quadrature rule
  this->elem_type = INVALID_ELEM;
  return;
}


template <unsigned int Dim, FEFamily T>
unsigned int FE<Dim,T>::n_quadrature_points () const
{
  libmesh_assert(this->qrule);
  return this->qrule->n_points();
}


template <unsigned int Dim, FEFamily T>
void FE<Dim,T>::dofs_on_side(const Elem * const elem,
                             const Order o,
                             unsigned int s,
                             std::vector<unsigned int> & di)
{
  libmesh_assert(elem);
  libmesh_assert_less (s, elem->n_sides());

  di.clear();
  unsigned int nodenum = 0;
  const unsigned int n_nodes = elem->n_nodes();
  for (unsigned int n = 0; n != n_nodes; ++n)
    {
      const unsigned int n_dofs = n_dofs_at_node(elem->type(),
                                                 static_cast<Order>(o + elem->p_level()), n);
      if (elem->is_node_on_side(n, s))
        for (unsigned int i = 0; i != n_dofs; ++i)
          di.push_back(nodenum++);
      else
        nodenum += n_dofs;
    }
}



template <unsigned int Dim, FEFamily T>
void FE<Dim,T>::dofs_on_edge(const Elem * const elem,
                             const Order o,
                             unsigned int e,
                             std::vector<unsigned int> & di)
{
  libmesh_assert(elem);
  libmesh_assert_less (e, elem->n_edges());

  di.clear();
  unsigned int nodenum = 0;
  const unsigned int n_nodes = elem->n_nodes();
  for (unsigned int n = 0; n != n_nodes; ++n)
    {
      const unsigned int n_dofs = n_dofs_at_node(elem->type(),
                                                 static_cast<Order>(o + elem->p_level()), n);
      if (elem->is_node_on_edge(n, e))
        for (unsigned int i = 0; i != n_dofs; ++i)
          di.push_back(nodenum++);
      else
        nodenum += n_dofs;
    }
}



template <unsigned int Dim, FEFamily T>
void FE<Dim,T>::reinit(const Elem * elem,
                       const std::vector<Point> * const pts,
                       const std::vector<Real> * const weights)
{
  // We can be called with no element.  If we're evaluating SCALAR
  // dofs we'll still have work to do.
  // libmesh_assert(elem);

  // We're calculating now!  Time to determine what.
  this->determine_calculations();

  // Try to avoid calling init_shape_functions
  // even when shapes_need_reinit
  bool cached_nodes_still_fit = false;

  // Most of the hard work happens when we have an actual element
  if (elem)
    {
      // Initialize the shape functions at the user-specified
      // points
      if (pts != nullptr)
        {
          // Set the type and p level for this element
          this->elem_type = elem->type();
          this->_p_level = elem->p_level();

          // Initialize the shape functions
          this->_fe_map->template init_reference_to_physical_map<Dim>
            (*pts, elem);
          this->init_shape_functions (*pts, elem);

          // The shape functions do not correspond to the qrule
          this->shapes_on_quadrature = false;
        }

      // If there are no user specified points, we use the
      // quadrature rule

      // update the type in accordance to the current cell
      // and reinit if the cell type has changed or (as in
      // the case of the hierarchics) the shape functions need
      // reinit, since they depend on the particular element shape
      else
        {
          libmesh_assert(this->qrule);
          this->qrule->init(elem->type(), elem->p_level());

          if (this->qrule->shapes_need_reinit())
            this->shapes_on_quadrature = false;

          if (this->elem_type != elem->type() ||
              this->_p_level != elem->p_level() ||
              !this->shapes_on_quadrature)
            {
              // Set the type and p level for this element
              this->elem_type = elem->type();
              this->_p_level = elem->p_level();
              // Initialize the shape functions
              this->_fe_map->template init_reference_to_physical_map<Dim>
                (this->qrule->get_points(), elem);
              this->init_shape_functions (this->qrule->get_points(), elem);

              if (this->shapes_need_reinit())
                {
                  cached_nodes.resize(elem->n_nodes());
                  for (auto n : elem->node_index_range())
                    cached_nodes[n] = elem->point(n);
                }
            }
          else
            {
              // libmesh_assert_greater (elem->n_nodes(), 1);

              cached_nodes_still_fit = true;
              if (cached_nodes.size() != elem->n_nodes())
                cached_nodes_still_fit = false;
              else
                for (auto n : make_range(1u, elem->n_nodes()))
                  {
                    if (!(elem->point(n) - elem->point(0)).relative_fuzzy_equals
                        ((cached_nodes[n] - cached_nodes[0]), 1e-13))
                      {
                        cached_nodes_still_fit = false;
                        break;
                      }
                  }

              if (this->shapes_need_reinit() && !cached_nodes_still_fit)
                {
                  this->_fe_map->template init_reference_to_physical_map<Dim>
                    (this->qrule->get_points(), elem);
                  this->init_shape_functions (this->qrule->get_points(), elem);
                  cached_nodes.resize(elem->n_nodes());
                  for (auto n : elem->node_index_range())
                    cached_nodes[n] = elem->point(n);
                }
            }

          // The shape functions correspond to the qrule
          this->shapes_on_quadrature = true;
        }
    }
  else // With no defined elem, so mapping or caching to
       // be done, and our "quadrature rule" is one point for nonlocal
       // (SCALAR) variables and zero points for local variables.
    {
      this->elem_type = INVALID_ELEM;
      this->_p_level = 0;

      if (!pts)
        {
          if (T == SCALAR)
            {
              this->qrule->get_points() =
                std::vector<Point>(1,Point(0));

              this->qrule->get_weights() =
                std::vector<Real>(1,1);
            }
          else
            {
              this->qrule->get_points().clear();
              this->qrule->get_weights().clear();
            }

          this->init_shape_functions (this->qrule->get_points(), elem);
        }
      else
        this->init_shape_functions (*pts, elem);
    }

  // Compute the map for this element.
  if (pts != nullptr)
    {
      if (weights != nullptr)
        {
          this->_fe_map->compute_map (this->dim, *weights, elem, this->calculate_d2phi);
        }
      else
        {
          std::vector<Real> dummy_weights (pts->size(), 1.);
          this->_fe_map->compute_map (this->dim, dummy_weights, elem, this->calculate_d2phi);
        }
    }
  else
    {
      this->_fe_map->compute_map (this->dim, this->qrule->get_weights(), elem, this->calculate_d2phi);
    }

  // Compute the shape functions and the derivatives at all of the
  // quadrature points.
  if (!cached_nodes_still_fit)
    {
      if (pts != nullptr)
        this->compute_shape_functions (elem,*pts);
      else
        this->compute_shape_functions(elem,this->qrule->get_points());
      if (this->calculate_dual)
      {
        if (T != LAGRANGE)
          nonlagrange_dual_warning();
        if (this->calculate_dual_coeff)
        {
          // In order for the matrices for the biorthogonality condition to be
          // non-singular, the dual basis coefficients must be computed when the
          // primal shape functions are evaluated with a quadrature rule. *But* a
          // user may be reiniting with integration points from a mortar segment,
          // which is valid, so a simple `if (!pts)` check is not
          // appropriate. We're just gonna have to trust the user on this one. If
          // they "screw up" we'll throw an exception from the LU decomposition,
          // and they can choose to handle it or not

          // instead of using the customized qrule from mortar segments,
          // we need the default qrule for computing the dual coefficients
          FEType default_fe_type(this->get_order(), T);
          QGauss default_qrule(elem->dim(), default_fe_type.default_quadrature_order());
          default_qrule.init(elem->type(), elem->p_level());
          // In preparation of computing dual_coeff, we compute the default shape
          // function values and save these in dual_phi (instead of declaring a
          // new container). The TRUE dual_phi values are computed in
          // compute_dual_shape_functions()
          all_shapes(elem, default_fe_type.order, default_qrule.get_points(), this->dual_phi);
          this->compute_dual_shape_coeffs(default_qrule);
          // only compute the dual coefficient once
          this->calculate_dual_coeff = false;
        }
        // the dual shape functions relies on the customized shape functions
        // and the coefficient matrix, \p dual_coeff
        this->compute_dual_shape_functions();
      }
    }
}

template <unsigned int Dim, FEFamily T>
void FE<Dim,T>::init_dual_shape_functions(const unsigned int n_shapes, const unsigned int n_qp)
{
  if (!this->calculate_dual)
    return;

  libmesh_assert_msg(this->calculate_phi,
                     "dual shape function calculation relies on "
                     "primal shape functions being calculated");

  this->dual_phi.resize(n_shapes);
  if (this->calculate_dphi)
    this->dual_dphi.resize(n_shapes);
#ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES
  if (this->calculate_d2phi)
    this->dual_d2phi.resize(n_shapes);
#endif

  for (auto i : index_range(this->dual_phi))
  {
    this->dual_phi[i].resize(n_qp);
    if (this->calculate_dphi)
      this->dual_dphi[i].resize(n_qp);
#ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES
  if (this->calculate_d2phi)
    this->dual_d2phi[i].resize(n_qp);
#endif
  }
}

template <unsigned int Dim, FEFamily T>
void FE<Dim,T>::init_shape_functions(const std::vector<Point> & qp,
                                     const Elem * elem)
{
  // Start logging the shape function initialization
  LOG_SCOPE("init_shape_functions()", "FE");

  // The number of quadrature points.
  const unsigned int n_qp = cast_int<unsigned int>(qp.size());

  // Number of shape functions in the finite element approximation
  // space.
  const unsigned int n_approx_shape_functions =
    this->n_shape_functions(this->get_type(),
                            this->get_order());

  // Maybe we already have correctly-sized data?  Check data sizes,
  // and get ready to break out of a "loop" if all these resize()
  // calls are redundant.
  unsigned int old_n_qp = 0;
  do
    {
      // resize the vectors to hold current data
      // Phi are the shape functions used for the FE approximation
      // Phi_map are the shape functions used for the FE mapping
      if (this->calculate_phi)
        {
          if (this->phi.size() == n_approx_shape_functions)
            {
              old_n_qp = n_approx_shape_functions ? this->phi[0].size() : 0;
              break;
            }
          this->phi.resize     (n_approx_shape_functions);
        }
      if (this->calculate_dphi)
        {
          if (this->dphi.size() == n_approx_shape_functions)
            {
              old_n_qp = n_approx_shape_functions ? this->dphi[0].size() : 0;
              break;
            }
          this->dphi.resize    (n_approx_shape_functions);
          this->dphidx.resize  (n_approx_shape_functions);
          this->dphidy.resize  (n_approx_shape_functions);
          this->dphidz.resize  (n_approx_shape_functions);
        }

      if (this->calculate_dphiref)
        {
          if (Dim > 0)
            {
              if (this->dphidxi.size() == n_approx_shape_functions)
                {
                  old_n_qp = n_approx_shape_functions ? this->dphidxi[0].size() : 0;
                  break;
                }
              this->dphidxi.resize (n_approx_shape_functions);
            }

          if (Dim > 1)
            this->dphideta.resize      (n_approx_shape_functions);

          if (Dim > 2)
            this->dphidzeta.resize     (n_approx_shape_functions);
        }

      if (this->calculate_curl_phi && (FEInterface::field_type(T) == TYPE_VECTOR))
        this->curl_phi.resize(n_approx_shape_functions);

      if (this->calculate_div_phi && (FEInterface::field_type(T) == TYPE_VECTOR))
        this->div_phi.resize(n_approx_shape_functions);

#ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES
      if (this->calculate_d2phi)
        {
          if (this->d2phi.size() == n_approx_shape_functions)
            {
              old_n_qp = n_approx_shape_functions ? this->d2phi[0].size() : 0;
              break;
            }

          this->d2phi.resize     (n_approx_shape_functions);
          this->d2phidx2.resize  (n_approx_shape_functions);
          this->d2phidxdy.resize (n_approx_shape_functions);
          this->d2phidxdz.resize (n_approx_shape_functions);
          this->d2phidy2.resize  (n_approx_shape_functions);
          this->d2phidydz.resize (n_approx_shape_functions);
          this->d2phidz2.resize  (n_approx_shape_functions);

          if (Dim > 0)
            this->d2phidxi2.resize (n_approx_shape_functions);

          if (Dim > 1)
            {
              this->d2phidxideta.resize (n_approx_shape_functions);
              this->d2phideta2.resize   (n_approx_shape_functions);
            }
          if (Dim > 2)
            {
              this->d2phidxidzeta.resize  (n_approx_shape_functions);
              this->d2phidetadzeta.resize (n_approx_shape_functions);
              this->d2phidzeta2.resize    (n_approx_shape_functions);
            }
        }
#endif // ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES
    }
  while (false);

  if (old_n_qp != n_qp)
    for (unsigned int i=0; i<n_approx_shape_functions; i++)
      {
        if (this->calculate_phi)
          this->phi[i].resize         (n_qp);

        if (this->calculate_dphi)
          {
            this->dphi[i].resize        (n_qp);
            this->dphidx[i].resize      (n_qp);
            this->dphidy[i].resize      (n_qp);
            this->dphidz[i].resize      (n_qp);
          }

        if (this->calculate_dphiref)
          {
            if (Dim > 0)
              this->dphidxi[i].resize(n_qp);

            if (Dim > 1)
              this->dphideta[i].resize(n_qp);

            if (Dim > 2)
              this->dphidzeta[i].resize(n_qp);
          }

        if (this->calculate_curl_phi && (FEInterface::field_type(T) == TYPE_VECTOR))
          this->curl_phi[i].resize(n_qp);

        if (this->calculate_div_phi && (FEInterface::field_type(T) == TYPE_VECTOR))
          this->div_phi[i].resize(n_qp);

#ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES
        if (this->calculate_d2phi)
          {
            this->d2phi[i].resize     (n_qp);
            this->d2phidx2[i].resize  (n_qp);
            this->d2phidxdy[i].resize (n_qp);
            this->d2phidxdz[i].resize (n_qp);
            this->d2phidy2[i].resize  (n_qp);
            this->d2phidydz[i].resize (n_qp);
            this->d2phidz2[i].resize  (n_qp);
            if (Dim > 0)
              this->d2phidxi2[i].resize (n_qp);
            if (Dim > 1)
              {
                this->d2phidxideta[i].resize (n_qp);
                this->d2phideta2[i].resize   (n_qp);
              }
            if (Dim > 2)
              {
                this->d2phidxidzeta[i].resize  (n_qp);
                this->d2phidetadzeta[i].resize (n_qp);
                this->d2phidzeta2[i].resize    (n_qp);
              }
          }
#endif // ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES
      }


#ifdef LIBMESH_ENABLE_INFINITE_ELEMENTS
  //------------------------------------------------------------
  // Initialize the data fields, which should only be used for infinite
  // elements, to some sensible values, so that using a FE with the
  // variational formulation of an InfFE, correct element matrices are
  // returned

  {
    this->weight.resize  (n_qp);
    this->dweight.resize (n_qp);
    this->dphase.resize  (n_qp);

    for (unsigned int p=0; p<n_qp; p++)
      {
        this->weight[p] = 1.;
        this->dweight[p].zero();
        this->dphase[p].zero();
      }

  }
#endif // ifdef LIBMESH_ENABLE_INFINITE_ELEMENTS

  switch (Dim)
    {

      //------------------------------------------------------------
      // 0D
    case 0:
      {
        break;
      }

      //------------------------------------------------------------
      // 1D
    case 1:
      {
        // Compute the value of the approximation shape function i at quadrature point p
        if (this->calculate_dphiref)
          for (unsigned int i=0; i<n_approx_shape_functions; i++)
            FE<Dim,T>::shape_derivs(elem, this->fe_type.order, i, 0, qp, this->dphidxi[i]);
#ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES
        if (this->calculate_d2phi)
          for (unsigned int i=0; i<n_approx_shape_functions; i++)
            for (unsigned int p=0; p<n_qp; p++)
              this->d2phidxi2[i][p] = FE<Dim,T>::shape_second_deriv (elem, this->fe_type.order, i, 0, qp[p]);
#endif // ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES

        break;
      }



      //------------------------------------------------------------
      // 2D
    case 2:
      {
        // Compute the value of the approximation shape function i at quadrature point p
        if (this->calculate_dphiref)
          for (unsigned int i=0; i<n_approx_shape_functions; i++)
            {
              FE<Dim,T>::shape_derivs(elem, this->fe_type.order, i, 0, qp, this->dphidxi[i]);
              FE<Dim,T>::shape_derivs(elem, this->fe_type.order, i, 1, qp, this->dphideta[i]);
            }
#ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES
        if (this->calculate_d2phi)
          for (unsigned int i=0; i<n_approx_shape_functions; i++)
            for (unsigned int p=0; p<n_qp; p++)
              {
                this->d2phidxi2[i][p] = FE<Dim,T>::shape_second_deriv (elem, this->fe_type.order, i, 0, qp[p]);
                this->d2phidxideta[i][p] = FE<Dim,T>::shape_second_deriv (elem, this->fe_type.order, i, 1, qp[p]);
                this->d2phideta2[i][p] = FE<Dim,T>::shape_second_deriv (elem, this->fe_type.order, i, 2, qp[p]);
              }
#endif // ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES


        break;
      }



      //------------------------------------------------------------
      // 3D
    case 3:
      {
        // Compute the value of the approximation shape function i at quadrature point p
        if (this->calculate_dphiref)
          for (unsigned int i=0; i<n_approx_shape_functions; i++)
            {
              FE<Dim,T>::shape_derivs(elem, this->fe_type.order, i, 0, qp, this->dphidxi[i]);
              FE<Dim,T>::shape_derivs(elem, this->fe_type.order, i, 1, qp, this->dphideta[i]);
              FE<Dim,T>::shape_derivs(elem, this->fe_type.order, i, 2, qp, this->dphidzeta[i]);
            }
#ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES
        if (this->calculate_d2phi)
          for (unsigned int i=0; i<n_approx_shape_functions; i++)
            for (unsigned int p=0; p<n_qp; p++)
              {
                this->d2phidxi2[i][p] = FE<Dim,T>::shape_second_deriv (elem, this->fe_type.order, i, 0, qp[p]);
                this->d2phidxideta[i][p] = FE<Dim,T>::shape_second_deriv (elem, this->fe_type.order, i, 1, qp[p]);
                this->d2phideta2[i][p] = FE<Dim,T>::shape_second_deriv (elem, this->fe_type.order, i, 2, qp[p]);
                this->d2phidxidzeta[i][p] = FE<Dim,T>::shape_second_deriv (elem, this->fe_type.order, i, 3, qp[p]);
                this->d2phidetadzeta[i][p] = FE<Dim,T>::shape_second_deriv (elem, this->fe_type.order, i, 4, qp[p]);
                this->d2phidzeta2[i][p] = FE<Dim,T>::shape_second_deriv (elem, this->fe_type.order, i, 5, qp[p]);
              }
#endif // ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES

        break;
      }


    default:
      libmesh_error_msg("Invalid dimension Dim = " << Dim);
    }

  if (this->calculate_dual)
    this->init_dual_shape_functions(n_approx_shape_functions, n_qp);
}



#ifdef LIBMESH_ENABLE_INFINITE_ELEMENTS

template <unsigned int Dim, FEFamily T>
void FE<Dim,T>::init_base_shape_functions(const std::vector<Point> & qp,
                                          const Elem * e)
{
  this->elem_type = e->type();
  this->_fe_map->template init_reference_to_physical_map<Dim>(qp, e);
  init_shape_functions(qp, e);
}

#endif // LIBMESH_ENABLE_INFINITE_ELEMENTS



//--------------------------------------------------------------
// Explicit instantiations using macro from fe_macro.h

INSTANTIATE_FE(0);

INSTANTIATE_FE(1);

INSTANTIATE_FE(2);

INSTANTIATE_FE(3);

INSTANTIATE_SUBDIVISION_FE;

} // namespace libMesh
