#include <iostream>

#include "db.hh"

namespace Db
{
    template <>
    bool Value::as<bool>()
    {
		return as<int>() != 0 ? true : false;
    }

    template <>
    std::string Value::as<std::string>()
    {
	return std::string( value_, len_ );
    }

    template <>
    Tempus::Time Value::as<Tempus::Time>()
    {
	Tempus::Time t;
 	int h, m, s;
 	sscanf( value_, "%d:%d:%d", &h, &m, &s );
 	t.n_secs = s + m * 60 + h * 3600;
	return t;
    }

    template <>
    long long Value::as<long long>()
    {
	long long v;
 	sscanf( value_, "%Ld", &v );
	return v;
    }
    template <>
    int Value::as<int>()
    {
	int v;
 	sscanf( value_, "%d", &v );
	return v;
    }
    template <>
    float Value::as<float>()
    {
	float v;
 	sscanf( value_, "%f", &v );
	return v;
    }
    template <>
    double Value::as<double>()
    {
	double v;
 	sscanf( value_, "%lf", &v );
	return v;
    }

};

