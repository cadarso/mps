// -*- mode: c++; fill-column: 80; c-basic-offset: 2; indent-tabs-mode: nil -*-
/*
    Copyright (c) 2010 Juan Jose Garcia Ripoll

    Tensor is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published
    by the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Library General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <tensor/linalg.h>
#include <mps/mps_algorithms.h>
#include <mps/time_evolve.h>
#include <tensor/io.h>

namespace mps {

  using namespace linalg;

  ArnoldiSolver::ArnoldiSolver(const Hamiltonian &H, cdouble dt, int nvectors) :
    TimeSolver(dt), H_(H, 0.0), max_states_(nvectors), tolerance_(1e-10)
  {
    if (max_states_ <= 0 || max_states_ >= 30) {
      std::cerr << "In ArnoldiSolver(...), the number of states exceeds the limits [1,30]"
		<< std::endl;
      abort();
    }
  }

  ArnoldiSolver::ArnoldiSolver(const CMPO &H, cdouble dt, int nvectors) :
    TimeSolver(dt), H_(H), max_states_(nvectors), tolerance_(1e-10)
  {
    if (max_states_ <= 0 || max_states_ >= 30) {
      std::cerr << "In ArnoldiSolver(...), the number of states exceeds the limits [1,30]"
		<< std::endl;
      abort();
    }
  }

  double
  ArnoldiSolver::one_step(CMPS *psi, index Dmax)
  {
    CTensor N = CTensor::zeros(max_states_, max_states_);
    CTensor Heff = N;

    std::vector<CMPS> states;
    states.reserve(max_states_);
    states.push_back(normal_form(*psi, -1));
    N.at(0,0) = to_complex(1.0);
    Heff.at(0,0) = expected(*psi, H_);

    std::vector<CMPS> vectors(3);
    std::vector<cdouble> coeffs(3);
    for (int ndx = 1; ndx < max_states_; ndx++) {
      const CMPS &last = states[ndx-1];
      //
      // 0) Estimate a new vector of the basis.
      //	current = H v[0] - <v[1]|H|v[0]> v[1] - <v[2]|H|v[0]> v[2]
      //    where
      //	v[0] = states[ndx-1]
      //	v[1] = states[ndx-2]
      //
      CMPS current = apply(H_, states.back());
      double n0 = norm2(current);
      {
	vectors.clear();
	coeffs.clear();

	vectors.push_back(current);
	coeffs.push_back(number_one<cdouble>());

	vectors.push_back(states[ndx-1]);
	coeffs.push_back(-scprod(current, states[ndx-1]));

	if (ndx > 1) {
	  vectors.push_back(states[ndx-2]);
          coeffs.push_back(-scprod(current, states[ndx-2]));
	}
        /*
	truncate(&current, vectors[0], 2*Dmax, false);
	simplify(&current, vectors, coeffs, NULL, 2, false);
        */
	simplify(&current, vectors, coeffs, 2*Dmax, -1, NULL, 2, false);
      }
      {
        double n = norm2(current);
        if (n < tolerance_ * std::max(n0, 1.0)) {
          N = N(range(0,ndx-1),range(0,ndx-1));
          Heff = Heff(range(0,ndx-1),range(0,ndx-1));
          break;
        }
        current = normal_form(current, -1);
      }

      //
      // 1) Add the matrices of the new vector to the whole set and, at the same time
      //    compute the scalar products of the old vectors with the new one.
      //    Also compute the matrix elements of the Hamiltonian in this new basis.
      //
      states.push_back(current);
      for (int n = 0; n <= ndx; n++) {
	cdouble aux;
	N.at(n, ndx) = aux = scprod(states[n], current);
	N.at(ndx, n) = conj(aux);
	Heff.at(n, ndx) = aux = expected(states[n], H_, current);
	Heff.at(ndx, n) = conj(aux);
      }
      N.at(ndx, ndx) = real(N(ndx,ndx));
      Heff.at(ndx, ndx) = real(Heff(ndx,ndx));
    }
    //
    // 2) Once we have the basis, we compute the exponential on it. Notice that, since
    //    our set of states is not orthonormal, we have to first orthogonalize it, then
    //    compute the exponential and finally move on to the original basis and build
    //    the approximate vector.
    //
    CTensor coef = CTensor::zeros(igen << Heff.rows());
    cdouble idt = to_complex(0, -1) * time_step();
    coef.at(0) = to_complex(1.0);
    coef = mmult(expm(idt * solve_with_svd(N, Heff)), coef);

    //
    // 4) Here is where we perform the truncation from our basis to a single MPS.
    //
    return simplify(psi, states, coef, Dmax, -1, NULL, 12, true);
  }

} // namespace mps
