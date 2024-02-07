// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Tyler Olsen
// =============================================================================

#ifndef CHFUNCT_LAMBDA_H
#define CHFUNCT_LAMBDA_H

#include "chrono/motion_functions/ChFunctionBase.h"
#include "chrono_thirdparty/yafel/DualNumber.hpp"

namespace chrono {

/// @addtogroup chrono_functions
/// @{

//
//
// Child of ChFunction designed to take a C++14 generic lambda as an argument.
// It provides analytical first and second derivatives via Dual Number forward-mode
// automatic differentiation, provided by the third party "yafel" finite element library.
// (https://github.com/tjolsen/YAFEL)
//
// Due to the template on the generic lambda type, it is difficult to construct this class in
// the usual manner. For this reason, a function called "make_ChFunctionLambda" has
// been provided to leverage modern C++ automatic return type deduction.
//
// For similar reasons, a utility function called "make_shared_ChFunctionLambda"
// is provided that returns a shared pointer to a heap-allocated ChFunctionLambda.
//
// Example usage:
// auto F = make_ChFunctionLambda( [](auto x) { return x*x; } );
// auto Fp = make_shared_ChFunctionLambda( [](auto x) { return x*x; } );
//
// @tparam LAMBDA Type of generic lambda passed to constructor.

template<typename LAMBDA>
class ChFunctionLambda : public ChFunction
{

private:
    LAMBDA function;


public:

    ChFunctionLambda(LAMBDA && _f) : function(_f) {}

    virtual ChFunctionLambda* Clone() const override { return new  ChFunctionLambda<LAMBDA>(*this); }

    virtual double Get_y(double x) const override { return function(x); }

    virtual double Get_y_dx(double x) const override { return function(yafel::DualNumber<double>(double(x),1.0)).second; }

    virtual double Get_y_dxdx(double x) const override {
        auto X = yafel::make_dual(yafel::make_dual(x));
        X.first.second = 1.0;
        X.second.first = 1.0;
        return function(X).second.second;
    }

    virtual FunctionType Get_Type() const override { return FUNCT_LAMBDA; }
};


// \brief Create a stack-allocated ChFunctionLambda
//
// @tparam T Type of generic lambda argument. Not necessary to type manually.
// @param func Generic lambda
//
template<typename T>
ChFunctionLambda<T> make_ChFunctionLambda(T && func) {
    return { std::forward<T>(func) };
}

///  \brief Create a shared pointer to a heap-allocated ChFunctionLambda
 // 
 // @tparam T Type of generic lambda argument. Not necessary to type manually.
 // @param func Generic lambda
 // 
template<typename T>
std::shared_ptr<ChFunctionLambda<T>> make_shared_ChFunctionLambda(T && func) {
    return chrono_types::make_shared<ChFunctionLambda<T>>(std::forward<T>(func));
}

/// @} chrono_functions

}  // end namespace chrono

#endif //!CHFUNCT_LAMBDA_H
