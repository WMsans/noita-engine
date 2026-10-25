#include "cell.h"
#include "coords.h"
#include "doctest/doctest.h"
#include "rng.h"

TEST_CASE("floor div/mod for negatives") {
	CHECK(floor_div(-1, 64) == -1);
	CHECK(floor_mod(-1, 64) == 63);
	CHECK(floor_div(-64, 64) == -1);
	CHECK(floor_div(-65, 64) == -2);
	CHECK(chunk_of(-1, 64).x == -1);
	CHECK(chunk_of(-1, 64).y == 1);
}

TEST_CASE("cell is 8 bytes") {
	CHECK(sizeof(Cell) == 8);
}

TEST_CASE("hash rng is stateless") {
	CHECK(hash5(1, 2, 3, 4, 5) == hash5(1, 2, 3, 4, 5));
	CHECK(hash5(1, 2, 3, 4, 5) != hash5(1, 2, 3, 4, 6));
	float u = hash_unit(hash5(7, 8, 9, 10, 11));
	CHECK(u >= 0.0f);
	CHECK(u < 1.0f);
}
