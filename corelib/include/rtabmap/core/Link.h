/*
 * Link.h
 *
 *  Created on: 2014-01-29
 *      Author: mathieu
 */

#ifndef LINK_H_
#define LINK_H_

#include <rtabmap/core/Transform.h>

namespace rtabmap {

class Link
{
public:
	enum Type {kNeighbor, kGlobalClosure, kLocalSpaceClosure, kLocalTimeClosure, kUserClosure, kUndef};
	Link() :
		from_(0),
		to_(0),
		type_(kUndef)
	{
	}
	Link(int from, int to, const Transform & transform, Type type) :
		from_(from),
		to_(to),
		transform_(transform),
		type_(type)
	{
	}

	bool isValid() const {return from_ > 0 && to_ > 0 && !transform_.isNull() && type_!=kUndef;}

	int from() const {return from_;}
	int to() const {return to_;}
	const Transform & transform() const {return transform_;}
	Type type() const {return type_;}

private:
	int from_;
	int to_;
	Transform transform_;
	Type type_;
};

}


#endif /* LINK_H_ */
