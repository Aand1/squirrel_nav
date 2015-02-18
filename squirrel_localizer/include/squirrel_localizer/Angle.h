// Angle.h --- 
// 
// Filename: Angle.h
// Description: 
// Author: Joerg Roewenkaemper
// Maintainer: boniardi@cs.uni-freiburg.de
// Created: Tue Feb 17 14:11:01 2015 (+0100)
// Version: 0.1.0
// Last-Updated: 
//           By: 
//     Update #: 0
// URL: 
// Keywords: 
// Compatibility: 
//    ROS Hydro
//    ROS Indigo
// 

// Code:

#ifndef SQUIRREL_LOCALIZER_ANGLE_H_
#define SQUIRREL_LOCALIZER_ANGLE_H_

#include "squirrel_localizer/RotationMatrix.h"

template <typename Base=double>
struct _Angle {
  static const int Dimension=2;
  static const int Angles=1;
  typedef Base BaseType;
  /**constructs a rotation of a radians*/
  _Angle(Base a=Base(0.)){
    _angle=a;
    _normalize();
  }

  /**constructs a rotation of v[0] radians*/
  _Angle(const _Vector<1, Base>& a){
    _angle=a[0];
    _normalize();
  }

  /**returns the rotation which result by composing this ad @param a.
   In 2d this is the sum of the angles*/
  _Angle<Base> operator  * ( const _Angle<Base>& a) const {
    return _Angle<Base>(_angle+a._angle);
  }

  /**accumulates a rotation*/
  _Angle<Base>& operator *= ( const _Angle<Base>& a) {
    _angle+=a._angle;
    _normalize();
    return *this;
  }

  /**applies a rotation to a vector*/
  _Vector<2,Base> operator * (const _Vector<2,Base>& v) const {
    return rotationMatrix()*v;
  }

  /**inverts a rotation*/
  _Angle<Base> inverse() const {
    return _Angle<Base>(-_angle);
  }

  /**returns the rotation matrix*/
  _RotationMatrix2<Base> rotationMatrix() const {
    return _RotationMatrix2<Base>(_angle);
  }

  /**returns the angle of the rotation*/
  inline Base angle() const {return _angle;}

  /**returns the angles of the rotation in a vector.
   It is provided to make an homogeneous interface with 
   the n dimensional rotations*/
  _Vector<1, Base> angles() const {_Vector<1, Base> v; v[0]=_angle; return v;}

  Base _angle;
  void _normalize() {
    if (_angle >= Base(0))
      _angle= fmod((Base)(_angle + Base(M_PI)),(Base)(2*M_PI)) - M_PI;
    else
      _angle= -(fmod((Base)(-_angle + M_PI),(Base)(2*M_PI)) - M_PI);
  }

  /**this allows an angle to be casted to a double*/
  operator Base () const  {
    return _angle;
  }

  Base operator [] (int) const {return _angle;}

  Base& operator [] (int) {return _angle;}

};
//@}

typedef _Angle<float>  Anglef;
typedef _Angle<double> Angle;

#endif  /* SQUIRREL_LOCALIZER_ANGLE_H_ */


// 
// Angle.h ends here
