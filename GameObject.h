#pragma once
#include <array>
#include <string>

struct Vector3
{
	float x, y, z;

	Vector3() : x(0), y(0), z(0) {}
	Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

	Vector3 operator+(const Vector3& other) const {
		return Vector3(x + other.x, y + other.y, z + other.z);
	}
	Vector3 operator*(float scalar) const {
		return Vector3(x * scalar, y * scalar, z * scalar);
	}
};

struct Color
{
	float r, g, b;
	Color(float r, float g, float b) : r(r), g(g), b(b) {}
};

//�÷��̾� ������Ʈ
struct Player
{
	int id;
	std::string nickname;
	Vector3 position;
	Vector3 velocity;
	Color color;
	bool active;

	//�Է�
	Vector3 inputMovement;

	Player(int id, Vector3 pos, Color col, std::string nick = "Player")
		: id(id), nickname(nick), position(pos), velocity(), color(col), active(true), inputMovement() {}
};

struct DummyObject
{
	int id;
	Vector3 position;
	Vector3 velocity;
	float radius;

	DummyObject(int id, Vector3 pos, Vector3 vel)
		: id(id), position(pos), velocity(vel), radius(0.5f) {
	}
};