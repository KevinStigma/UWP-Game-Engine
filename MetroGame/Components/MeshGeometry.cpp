#include "pch.h"
#include "MeshGeometry.h"
#include "Common/DirectXHelper.h"
#include "Common/MathHelper.h"

using namespace Microsoft::WRL;
using namespace DXFramework;
using namespace DirectX;

using namespace DX;

Keyframe::Keyframe()
	: TimePos(0.0f),
	Translation(0.0f, 0.0f, 0.0f),
	Scale(1.0f, 1.0f, 1.0f),
	RotationQuat(0.0f, 0.0f, 0.0f, 1.0f)
{
}

Keyframe::~Keyframe()
{
}

float BoneAnimation::GetStartTime()const
{
	// Keyframes are sorted by time, so first keyframe gives start time.
	return Keyframes.front().TimePos;
}

float BoneAnimation::GetEndTime()const
{
	// Keyframes are sorted by time, so last keyframe gives end time.
	float f = Keyframes.back().TimePos;

	return f;
}

void BoneAnimation::Interpolate(float t, XMFLOAT4X4& M)const
{
	if (t <= Keyframes.front().TimePos)
	{
		XMVECTOR S = XMLoadFloat3(&Keyframes.front().Scale);
		XMVECTOR P = XMLoadFloat3(&Keyframes.front().Translation);
		XMVECTOR Q = XMLoadFloat4(&Keyframes.front().RotationQuat);

		XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		XMStoreFloat4x4(&M, XMMatrixAffineTransformation(S, zero, Q, P));
	}
	else if (t >= Keyframes.back().TimePos)
	{
		XMVECTOR S = XMLoadFloat3(&Keyframes.back().Scale);
		XMVECTOR P = XMLoadFloat3(&Keyframes.back().Translation);
		XMVECTOR Q = XMLoadFloat4(&Keyframes.back().RotationQuat);

		XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		XMStoreFloat4x4(&M, XMMatrixAffineTransformation(S, zero, Q, P));
	}
	else
	{
		for (UINT i = 0; i < Keyframes.size() - 1; ++i)
		{
			if (t >= Keyframes[i].TimePos && t <= Keyframes[i + 1].TimePos)
			{
				float lerpPercent = (t - Keyframes[i].TimePos) / (Keyframes[i + 1].TimePos - Keyframes[i].TimePos);

				XMVECTOR s0 = XMLoadFloat3(&Keyframes[i].Scale);
				XMVECTOR s1 = XMLoadFloat3(&Keyframes[i + 1].Scale);

				XMVECTOR p0 = XMLoadFloat3(&Keyframes[i].Translation);
				XMVECTOR p1 = XMLoadFloat3(&Keyframes[i + 1].Translation);

				XMVECTOR q0 = XMLoadFloat4(&Keyframes[i].RotationQuat);
				XMVECTOR q1 = XMLoadFloat4(&Keyframes[i + 1].RotationQuat);

				XMVECTOR S = XMVectorLerp(s0, s1, lerpPercent);
				XMVECTOR P = XMVectorLerp(p0, p1, lerpPercent);
				XMVECTOR Q = XMQuaternionSlerp(q0, q1, lerpPercent);

				XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
				XMStoreFloat4x4(&M, XMMatrixAffineTransformation(S, zero, Q, P));

				break;
			}
		}
	}
}

float AnimationClip::GetClipStartTime()const
{
	// Find smallest start time over all bones in this clip.
	float t = MathHelper::Infinity;
	for (UINT i = 0; i < BoneAnimations.size(); ++i)
	{
		t = MathHelper::Min(t, BoneAnimations[i].GetStartTime());
	}

	return t;
}

float AnimationClip::GetClipEndTime()const
{
	// Find largest end time over all bones in this clip.
	float t = 0.0f;
	for (UINT i = 0; i < BoneAnimations.size(); ++i)
	{
		t = MathHelper::Max(t, BoneAnimations[i].GetEndTime());
	}

	return t;
}

void AnimationClip::Interpolate(float t, std::vector<XMFLOAT4X4>& boneTransforms)const
{
	for (UINT i = 0; i < BoneAnimations.size(); ++i)
	{
		BoneAnimations[i].Interpolate(t, boneTransforms[i]);
	}
}

float SkinnedData::GetClipStartTime(const std::wstring& clipName)const
{
	auto clip = m_animations.find(clipName);
	if (clip == m_animations.end())
		throw ref new Platform::InvalidArgumentException("No such animation data!");
	return clip->second.GetClipStartTime();
}

float SkinnedData::GetClipEndTime(const std::wstring& clipName)const
{
	auto clip = m_animations.find(clipName);
	if (clip == m_animations.end())
		throw ref new Platform::InvalidArgumentException("No such animation data!");
	return clip->second.GetClipEndTime();
}

UINT SkinnedData::GetBoneCount()const
{
	return m_boneOffsets.size();
}

void SkinnedData::Initialize(std::vector<XMFLOAT4X4>& boneOffsets,
	std::map<std::wstring, AnimationClip>& animations)
{
	m_boneOffsets = boneOffsets;
	m_animations = animations;
}

void SkinnedData::GetFinalTransforms(const std::wstring& clipName, float timePos, std::vector<XMFLOAT4X4>& finalTransforms)const
{
	UINT numBones = m_boneOffsets.size();
	std::vector<XMFLOAT4X4> boneTransforms(numBones);

	// Interpolate all the bones of this clip at the given time instance.
	auto clip = m_animations.find(clipName);
	if (clip == m_animations.end())
		throw ref new Platform::InvalidArgumentException("No such animation data!");
	clip->second.Interpolate(timePos, boneTransforms);

	// Premultiply by the bone offset transform to get the final transform.
	for (UINT i = 0; i < numBones; ++i)
	{
		XMMATRIX offset = XMLoadFloat4x4(&m_boneOffsets[i]);
		XMMATRIX bone = XMLoadFloat4x4(&boneTransforms[i]);
		// Pre-transpose
		XMStoreFloat4x4(&finalTransforms[i], XMMatrixTranspose(XMMatrixMultiply(offset, bone)));
	}
}