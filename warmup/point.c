#include <assert.h>
#include "common.h"
#include "point.h"
#include <math.h>

void
point_translate(struct point *p, double x, double y)
{
	p->x += x;
	p->y += y;
}

double
point_distance(const struct point *p1, const struct point *p2)
{
	double x_diff = p2->x - p1->x;
	double y_diff = p2->y - p1->y;
	double distance = sqrt(x_diff * x_diff + y_diff * y_diff);
	return distance;
}

int
point_compare(const struct point *p1, const struct point *p2)
{
	struct point origin;
	point_set(&origin, 0.0, 0.0);
	double d1 = point_distance(&origin, p1);
	double d2 = point_distance(&origin, p2);
	if (d1 < d2)
		return -1;
	if (d1 > d2)
		return 1;
	return 0;
}
