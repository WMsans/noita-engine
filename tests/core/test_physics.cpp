#include "box2d/box2d.h"
#include "doctest/doctest.h"

TEST_CASE("box2d links and a box falls") {
	b2SetLengthUnitsPerMeter(10.0f);
	b2WorldDef wd = b2DefaultWorldDef();
	wd.gravity = {0.0f, 600.0f};
	const b2WorldId world = b2CreateWorld(&wd);
	b2BodyDef bd = b2DefaultBodyDef();
	bd.type = b2_dynamicBody;
	const b2BodyId body = b2CreateBody(world, &bd);
	const b2ShapeDef sd = b2DefaultShapeDef();
	const b2Polygon box = b2MakeBox(2.0f, 2.0f);
	b2CreatePolygonShape(body, &sd, &box);
	for (int i = 0; i < 60; ++i) b2World_Step(world, 1.0f / 60.0f, 4);
	CHECK(b2Body_GetPosition(body).y > 200.0f);
	b2DestroyWorld(world);
}
