#pragma once
#include <PxPhysicsAPI.h>
#include "GameObject.h"
#include <memory>
#include <unordered_map>
#include <iostream>

using namespace physx;
using namespace std;

//PhysX ���� �ݹ�
class PhysXErrorCallback : public PxErrorCallback
{
public:
	virtual void reportError(PxErrorCode::Enum code, const char* message, const char* file, int line) override
	{
		cerr << "PhysX Error [" << file << ":" << line << "]: " << message << endl;
	}
};

//PhysX ���� ���� ����
class PhysicsWorld
{
private:
	PxDefaultAllocator allocator_;
	PhysXErrorCallback errorCallback_;
	PxFoundation* foundation_ = nullptr;
	PxPhysics* physics_ = nullptr;
	PxDefaultCpuDispatcher* dispatcher_ = nullptr;
	PxScene* scene_ = nullptr;
	PxMaterial* defaultMaterial_ = nullptr;

	unordered_map<int, PxRigidDynamic*> playerActors_;
	unordered_map<int, PxRigidDynamic*> dummyActors_;

	unordered_map<int, float> dummyJumpTimers_;
	const float JUMP_INTERVAL = 1.0f; // 1�ʸ��� ����
	const float JUMP_FORCE = 50.0f; // ���� ����(���� ��)

public:
	PhysicsWorld()
	{
		initPhysX();
	}
	~PhysicsWorld()
	{
		cleanup();
	}

	void initPhysX()
	{
		cout << "Initializing PhysX SDK..." << endl;
		
		foundation_ = PxCreateFoundation(PX_PHYSICS_VERSION, allocator_, errorCallback_);
		if (!foundation_)
		{
			cerr << "PxCreateFoundation failed!" << endl;
			return;
		}
		cout << "PhysX Foundation created (Version: " << PX_PHYSICS_VERSION << ")" << endl;

		PxTolerancesScale scale;
		physics_ = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation_, scale, true);
		if (!physics_)
		{
			cerr << "pxCreatePhysics failed!" << endl;
			return;
		}
		cout << "PhysX Physics created" << endl;

		PxSceneDesc sceneDesc(physics_->getTolerancesScale());
		sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);

		dispatcher_ = PxDefaultCpuDispatcherCreate(2);
		sceneDesc.cpuDispatcher = dispatcher_;
		sceneDesc.filterShader = PxDefaultSimulationFilterShader;

		scene_ = physics_->createScene(sceneDesc);
		if (!scene_)
		{
			cerr << "createScene failed!" << endl;
			return;
		}
		cout << "PhysX Scene created" << endl;

		// �ݹ߷� ���� Material
		//(��������,��������,�ݹ߰��)
		defaultMaterial_ = physics_->createMaterial(0.6f, 0.5f, 0.5f);
		createGround();

		cout << "PhysX initialization complete!" << endl;
	}
	void createGround()
	{
		PxRigidStatic* groundPlane = PxCreatePlane(*physics_, PxPlane(0, 1, 0, 0), *defaultMaterial_);
		scene_->addActor(*groundPlane);
		cout << "Ground plane created" << endl;
	}

	PxRigidDynamic* createPlayerActor(int playerId, const Vector3& pos)
	{
		PxShape* shape = physics_->createShape(
			PxBoxGeometry(0.5f, 0.5f,0.5f), //Half extents
			*defaultMaterial_
		);

		PxTransform transform(PxVec3(pos.x, pos.y, pos.z));
		PxRigidDynamic* actor = physics_->createRigidDynamic(transform);

		actor->attachShape(*shape);
		shape->release();

		PxRigidBodyExt::updateMassAndInertia(*actor, 15.0f); //��ü ����

		//ȸ�� ����
		actor->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_X, true);
		actor->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y, true);
		actor->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z, true);

		actor->setLinearDamping(0.5f);
		actor->setMaxLinearVelocity(20.0f); //�÷��̾� �ӵ�

		scene_->addActor(*actor);
		playerActors_[playerId] = actor;

		cout << "Player " << playerId << "actor created" << endl;
		return actor;
		actor->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_X, true);
		actor->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_X, true);
	}

	PxRigidDynamic* createDummyActor(int dummyId, const Vector3& pos)
	{
		PxShape* shape = physics_->createShape(
			PxBoxGeometry(0.5f,0.5f,0.5f),
			*defaultMaterial_
		);
		
		float yPos = pos.y < 0.5f ? 0.5f : pos.y; //�ּ� 0.5 �ٴ� ��
		PxTransform transform(PxVec3(pos.x, pos.y, pos.z));

		PxRigidDynamic* actor = physics_->createRigidDynamic(transform);

		actor->attachShape(*shape);
		shape->release();

		PxRigidBodyExt::updateMassAndInertia(*actor, 5.0f);

		actor->setSleepThreshold(0.0f); // �浹�� �ٷ� ����
		actor->setLinearDamping(0.3f); // ������ �ս�
		actor->setMaxLinearVelocity(20.0f);
		
		scene_->addActor(*actor);
		dummyActors_[dummyId] = actor;

		dummyJumpTimers_[dummyId] = (rand() % 100) / 100.0f;

		return actor;
	}
	void applyPlayerInput(int playerId, const Vector3& movement)
	{
		auto it = playerActors_.find(playerId);
		if (it != playerActors_.end() && it->second)
		{
			float speed = 12.0f;
			PxVec3 desiredVel(movement.x * speed, 0.0f, movement.z * speed);

			PxVec3 currentVel = it->second->getLinearVelocity();
			desiredVel.y = currentVel.y;

			it->second->setLinearVelocity(desiredVel);
		}
	}

	void applyPlayerJump(int playerId)
	{
		auto it = playerActors_.find(playerId);
		if (it != playerActors_.end() && it->second)
		{
			PxRigidDynamic* actor = it->second;
			PxVec3 velocity = actor->getLinearVelocity();

			// 땅에 있을 때만 점프 (y속도가 작을 때)
			if (abs(velocity.y) < 0.5f)
			{
				PxTransform pose = actor->getGlobalPose();
				// 바닥 근처에 있을 때만
				if (pose.p.y < 2.0f)
				{
					const float PLAYER_JUMP_FORCE = 150.0f;
					PxVec3 jumpForce(0.0f, PLAYER_JUMP_FORCE, 0.0f);
					actor->addForce(jumpForce, PxForceMode::eIMPULSE);
					cout << "Player " << playerId << " jumped!" << endl;
				}
			}
		}
	}
	void updateDummies(float deltaTime)
	{
		for (auto& pair : dummyActors_)
		{
			int dummyId = pair.first;
			PxRigidDynamic* actor = pair.second;
			
			if (!actor) continue;

			PxVec3 velocity = actor->getLinearVelocity();
			bool isOnGround = abs(velocity.y) < 0.5f;

			//�ٴڿ� �ְ�, ��ġ�� ������ ����
			PxTransform pose = actor->getGlobalPose();
			bool isLowEnough = pose.p.y < 1.0f;//�ٴ� ��ó

			if (isOnGround && isLowEnough)
			{
				dummyJumpTimers_[dummyId] += deltaTime;
				if (dummyJumpTimers_[dummyId] >= JUMP_INTERVAL)
				{
					dummyJumpTimers_[dummyId] = 0.0f;

					//����
					PxVec3 jumpForce(0.0f, JUMP_FORCE, 0.0f);
					actor->addForce(jumpForce, PxForceMode::eIMPULSE);
				}
			}
			else
			{
				//���߿� ������ Ÿ�̸� ����(���� ����)
			}
		}
	}

	void simulate(float deltaTime)
	{
		if (scene_)
		{
			//���� ���� ������Ʈ
			updateDummies(deltaTime);

			//���� �ùķ��̼�
			float fixedStep = 1.0f / 60.0f;
			if (deltaTime > fixedStep * 2.0f)
			{
				deltaTime = fixedStep * 2.0f;
			}
			scene_->simulate(deltaTime);
			scene_->fetchResults(true);
		}
	}
	Vector3 getPlayerPosition(int playerId)
	{
		auto it = playerActors_.find(playerId);
		if (it != playerActors_.end() && it->second)
		{
			PxTransform transform = it->second->getGlobalPose();
			return Vector3(transform.p.x, transform.p.y, transform.p.z);
		}
		return Vector3();
	}

	Vector3 getPlayerVelocity(int playerId)
	{
		auto it = playerActors_.find(playerId);
		if (it != playerActors_.end() && it->second)
		{
			PxVec3 vel = it->second->getLinearVelocity();
			return Vector3(vel.x, vel.y, vel.z);
		}
		return Vector3();
	}
	Vector3 getDummyPosition(int dummyId)
	{
		auto it = dummyActors_.find(dummyId);
		if (it != dummyActors_.end() && it->second)
		{
			PxTransform transform = it->second->getGlobalPose();
			return Vector3(transform.p.x, transform.p.y, transform.p.z);
		}
		return Vector3();
	}
	void removePlayer(int playerId)
	{
		auto it = playerActors_.find(playerId);
		if (it != playerActors_.end() && it->second)
		{
			it->second->release();
			playerActors_.erase(it);
			cout << "Player " << playerId << " actor removed" << endl;
		}
	}
	void removeDummy(int dummyId)
	{
		auto actorIt = dummyActors_.find(dummyId);
		if (actorIt != dummyActors_.end() && actorIt->second)
		{
			actorIt->second->release();
			dummyActors_.erase(actorIt);
		}
		dummyJumpTimers_.erase(dummyId);
	}
	void cleanup()
	{
		cout << "Cleaning up PhysX..." << endl;

		for (auto& pair : playerActors_)
		{
			if (pair.second) pair.second->release();
		}
		for (auto& pair : dummyActors_)
		{
			if (pair.second) pair.second->release();
		}

		if (scene_) scene_->release();
		if (dispatcher_) dispatcher_->release();
		if (physics_) physics_->release();
		if (foundation_) foundation_->release();

		cout << "PhysX cleaned up" << endl;
	}
};