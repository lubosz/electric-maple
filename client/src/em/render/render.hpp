// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

/*!
 * @file
 * @brief Very simple GLES3 renderer for WebRTC client.
 * @author Moshi Turner <moses@collabora.com>
 * @author Rylie Pavlik <rpavlik@collabora.com>
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 */

#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <memory>
#include <array>

/*!
  * Refer to the notes in AdditiveSimFragShader to understand the reasoning
  * for the default value.
  */
inline constexpr const float DefaultBlackThreshold = 16.f/255.f;

class Renderer
{
public:
	Renderer() = default;
	~Renderer();

	Renderer(const Renderer &) = delete;
	Renderer(Renderer &&) = delete;

	Renderer &
	operator=(const Renderer &) = delete;
	Renderer &
	operator=(Renderer &&) = delete;

	/// Create resources. Must call with EGL Context current
	void
	setupRender();

	/// Destroy resources. Must call with EGL context current.
	void
	reset();

	struct DrawInfo {
		GLuint texture;
		GLenum texture_target;
		struct {
			float black_threshold{DefaultBlackThreshold};
			bool  enable{false};
		} alpha_for_additive;
	};
	/// Draw texture to framebuffer. Must call with EGL Context current.
	void
	draw(const DrawInfo& drawInfo) const;

private:
	void
	setupShaders();
	void
	setupQuadVertexData();

	struct Program final {
		GLuint id = 0;
		GLint textureSamplerLocation = 0;
		GLint blackThresholdLocation = 0;
	};
	std::array<Program,2> programs = {};
	GLuint quadVAO = 0;
	GLuint quadVBO = 0;

	float blackThreshold = DefaultBlackThreshold;
};
