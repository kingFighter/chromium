// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop.h"
#include "cc/layers/append_quads_data.h"
#include "cc/output/gl_renderer.h"
#include "cc/quads/draw_quad.h"
#include "cc/resources/sync_point_helper.h"
#include "cc/test/pixel_test.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "third_party/skia/include/core/SkImageFilter.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/effects/SkColorFilterImageFilter.h"
#include "third_party/skia/include/effects/SkColorMatrixFilter.h"

namespace cc {
namespace {

scoped_ptr<RenderPass> CreateTestRootRenderPass(RenderPass::Id id,
                                                gfx::Rect rect) {
  scoped_ptr<RenderPass> pass = RenderPass::Create();
  const gfx::Rect output_rect = rect;
  const gfx::RectF damage_rect = rect;
  const gfx::Transform transform_to_root_target;
  pass->SetNew(id, output_rect, damage_rect, transform_to_root_target);
  return pass.Pass();
}

scoped_ptr<RenderPass> CreateTestRenderPass(
    RenderPass::Id id,
    gfx::Rect rect,
    const gfx::Transform& transform_to_root_target) {
  scoped_ptr<RenderPass> pass = RenderPass::Create();
  const gfx::Rect output_rect = rect;
  const gfx::RectF damage_rect = rect;
  pass->SetNew(id, output_rect, damage_rect, transform_to_root_target);
  return pass.Pass();
}

scoped_ptr<SharedQuadState> CreateTestSharedQuadState(
    gfx::Transform content_to_target_transform, gfx::Rect rect) {
  const gfx::Size content_bounds = rect.size();
  const gfx::Rect visible_content_rect = rect;
  const gfx::Rect clip_rect = rect;
  const bool is_clipped = false;
  const float opacity = 1.0f;
  scoped_ptr<SharedQuadState> shared_state = SharedQuadState::Create();
  shared_state->SetAll(content_to_target_transform,
                       content_bounds,
                       visible_content_rect,
                       clip_rect,
                       is_clipped,
                       opacity);
  return shared_state.Pass();
}

scoped_ptr<DrawQuad> CreateTestRenderPassDrawQuad(
    SharedQuadState* shared_state, gfx::Rect rect, RenderPass::Id pass_id) {
  scoped_ptr<RenderPassDrawQuad> quad = RenderPassDrawQuad::Create();
  quad->SetNew(shared_state,
               rect,
               pass_id,
               false,         // is_replica
               0,             // mask_resource_id
               rect,          // contents_changed_since_last_frame
               gfx::RectF(),  // mask_uv_rect
               WebKit::WebFilterOperations(),   // foreground filters
               skia::RefPtr<SkImageFilter>(),   // foreground filter
               WebKit::WebFilterOperations());  // background filters

  return quad.PassAs<DrawQuad>();
}

typedef ::testing::Types<GLRenderer, SoftwareRenderer> RendererTypes;
TYPED_TEST_CASE(RendererPixelTest, RendererTypes);

// All pixels can be off by one, but any more than that is an error.
class FuzzyPixelOffByOneComparator : public FuzzyPixelComparator {
 public:
  explicit FuzzyPixelOffByOneComparator(bool discard_alpha)
    : FuzzyPixelComparator(discard_alpha, 100.f, 0.f, 1.f, 1, 0) {}
};

template <typename RendererType>
class FuzzyForSoftwareOnlyPixelComparator : public PixelComparator {
 public:
  explicit FuzzyForSoftwareOnlyPixelComparator(bool discard_alpha)
      : fuzzy_(discard_alpha), exact_(discard_alpha) {}

  virtual bool Compare(const SkBitmap& actual_bmp,
                       const SkBitmap& expected_bmp) const;

 private:
  FuzzyPixelOffByOneComparator fuzzy_;
  ExactPixelComparator exact_;
};

template<>
bool FuzzyForSoftwareOnlyPixelComparator<GLRenderer>::Compare(
    const SkBitmap& actual_bmp,
    const SkBitmap& expected_bmp) const {
  return exact_.Compare(actual_bmp, expected_bmp);
}

template<>
bool FuzzyForSoftwareOnlyPixelComparator<SoftwareRenderer>::Compare(
    const SkBitmap& actual_bmp,
    const SkBitmap& expected_bmp) const {
  return fuzzy_.Compare(actual_bmp, expected_bmp);
}

#if !defined(OS_ANDROID)
TYPED_TEST(RendererPixelTest, SimpleGreenRect) {
  gfx::Rect rect(this->device_viewport_size_);

  RenderPass::Id id(1, 1);
  scoped_ptr<RenderPass> pass = CreateTestRootRenderPass(id, rect);

  gfx::Transform content_to_target_transform;
  scoped_ptr<SharedQuadState> shared_state =
      CreateTestSharedQuadState(content_to_target_transform, rect);

  scoped_ptr<SolidColorDrawQuad> color_quad = SolidColorDrawQuad::Create();
  color_quad->SetNew(shared_state.get(), rect, SK_ColorGREEN);

  pass->quad_list.push_back(color_quad.PassAs<DrawQuad>());

  RenderPassList pass_list;
  pass_list.push_back(pass.Pass());

  EXPECT_TRUE(this->RunPixelTest(
      &pass_list,
      base::FilePath(FILE_PATH_LITERAL("green.png")),
      ExactPixelComparator(true)));
}

TYPED_TEST(RendererPixelTest, FastPassColorFilterAlpha) {
  gfx::Rect viewport_rect(this->device_viewport_size_);

  RenderPass::Id root_pass_id(1, 1);
  scoped_ptr<RenderPass> root_pass =
      CreateTestRootRenderPass(root_pass_id, viewport_rect);

  RenderPass::Id child_pass_id(2, 2);
  gfx::Rect pass_rect(this->device_viewport_size_);
  gfx::Transform transform_to_root;
  scoped_ptr<RenderPass> child_pass =
      CreateTestRenderPass(child_pass_id, pass_rect, transform_to_root);

  gfx::Transform content_to_target_transform;
  scoped_ptr<SharedQuadState> shared_state =
      CreateTestSharedQuadState(content_to_target_transform, viewport_rect);
  shared_state->opacity = 0.5f;

  scoped_ptr<SolidColorDrawQuad> blue = SolidColorDrawQuad::Create();
  blue->SetNew(shared_state.get(),
               gfx::Rect(0,
                         0,
                         this->device_viewport_size_.width() / 2,
                         this->device_viewport_size_.height()),
               SK_ColorBLUE);
  scoped_ptr<SolidColorDrawQuad> yellow = SolidColorDrawQuad::Create();
  yellow->SetNew(shared_state.get(),
                 gfx::Rect(this->device_viewport_size_.width() / 2,
                           0,
                           this->device_viewport_size_.width() / 2,
                           this->device_viewport_size_.height()),
                 SK_ColorYELLOW);

  scoped_ptr<SharedQuadState> blank_state =
      CreateTestSharedQuadState(content_to_target_transform, viewport_rect);

  scoped_ptr<SolidColorDrawQuad> white = SolidColorDrawQuad::Create();
  white->SetNew(blank_state.get(),
                viewport_rect,
                SK_ColorWHITE);

  child_pass->quad_list.push_back(blue.PassAs<DrawQuad>());
  child_pass->quad_list.push_back(yellow.PassAs<DrawQuad>());
  child_pass->quad_list.push_back(white.PassAs<DrawQuad>());

  scoped_ptr<SharedQuadState> pass_shared_state =
      CreateTestSharedQuadState(gfx::Transform(), pass_rect);

  SkScalar matrix[20];
  float amount = 0.5f;
  matrix[0] = 0.213f + 0.787f * amount;
  matrix[1] = 0.715f - 0.715f * amount;
  matrix[2] = 1.f - (matrix[0] + matrix[1]);
  matrix[3] = matrix[4] = 0;
  matrix[5] = 0.213f - 0.213f * amount;
  matrix[6] = 0.715f + 0.285f * amount;
  matrix[7] = 1.f - (matrix[5] + matrix[6]);
  matrix[8] = matrix[9] = 0;
  matrix[10] = 0.213f - 0.213f * amount;
  matrix[11] = 0.715f - 0.715f * amount;
  matrix[12] = 1.f - (matrix[10] + matrix[11]);
  matrix[13] = matrix[14] = 0;
  matrix[15] = matrix[16] = matrix[17] = matrix[19] = 0;
  matrix[18] = 1;
  skia::RefPtr<SkColorFilter> colorFilter(skia::AdoptRef(
      new SkColorMatrixFilter(matrix)));
  skia::RefPtr<SkImageFilter> filter =
      skia::AdoptRef(SkColorFilterImageFilter::Create(colorFilter.get(), NULL));

  scoped_ptr<RenderPassDrawQuad> render_pass_quad =
      RenderPassDrawQuad::Create();
  render_pass_quad->SetNew(pass_shared_state.get(),
                           pass_rect,
                           child_pass_id,
                           false,
                           0,
                           pass_rect,
                           gfx::RectF(),
                           WebKit::WebFilterOperations(),
                           filter,
                           WebKit::WebFilterOperations());

  root_pass->quad_list.push_back(render_pass_quad.PassAs<DrawQuad>());

  RenderPassList pass_list;
  pass_list.push_back(child_pass.Pass());
  pass_list.push_back(root_pass.Pass());

  // This test has alpha=254 for the software renderer vs. alpha=255 for the gl
  // renderer so use a fuzzy comparator.
  EXPECT_TRUE(this->RunPixelTest(
      &pass_list,
      base::FilePath(FILE_PATH_LITERAL("blue_yellow_alpha.png")),
      FuzzyForSoftwareOnlyPixelComparator<TypeParam>(false)));
}

TYPED_TEST(RendererPixelTest, FastPassColorFilterAlphaTranslation) {
  gfx::Rect viewport_rect(this->device_viewport_size_);

  RenderPass::Id root_pass_id(1, 1);
  scoped_ptr<RenderPass> root_pass =
      CreateTestRootRenderPass(root_pass_id, viewport_rect);

  RenderPass::Id child_pass_id(2, 2);
  gfx::Rect pass_rect(this->device_viewport_size_);
  gfx::Transform transform_to_root;
  scoped_ptr<RenderPass> child_pass =
      CreateTestRenderPass(child_pass_id, pass_rect, transform_to_root);

  gfx::Transform content_to_target_transform;
  scoped_ptr<SharedQuadState> shared_state =
      CreateTestSharedQuadState(content_to_target_transform, viewport_rect);
  shared_state->opacity = 0.5f;

  scoped_ptr<SolidColorDrawQuad> blue = SolidColorDrawQuad::Create();
  blue->SetNew(shared_state.get(),
               gfx::Rect(0,
                         0,
                         this->device_viewport_size_.width() / 2,
                         this->device_viewport_size_.height()),
               SK_ColorBLUE);
  scoped_ptr<SolidColorDrawQuad> yellow = SolidColorDrawQuad::Create();
  yellow->SetNew(shared_state.get(),
                 gfx::Rect(this->device_viewport_size_.width() / 2,
                           0,
                           this->device_viewport_size_.width() / 2,
                           this->device_viewport_size_.height()),
                 SK_ColorYELLOW);

  scoped_ptr<SharedQuadState> blank_state =
      CreateTestSharedQuadState(content_to_target_transform, viewport_rect);

  scoped_ptr<SolidColorDrawQuad> white = SolidColorDrawQuad::Create();
  white->SetNew(blank_state.get(),
                viewport_rect,
                SK_ColorWHITE);

  child_pass->quad_list.push_back(blue.PassAs<DrawQuad>());
  child_pass->quad_list.push_back(yellow.PassAs<DrawQuad>());
  child_pass->quad_list.push_back(white.PassAs<DrawQuad>());

  scoped_ptr<SharedQuadState> pass_shared_state =
      CreateTestSharedQuadState(gfx::Transform(), pass_rect);

  SkScalar matrix[20];
  float amount = 0.5f;
  matrix[0] = 0.213f + 0.787f * amount;
  matrix[1] = 0.715f - 0.715f * amount;
  matrix[2] = 1.f - (matrix[0] + matrix[1]);
  matrix[3] = 0;
  matrix[4] = 20.f;
  matrix[5] = 0.213f - 0.213f * amount;
  matrix[6] = 0.715f + 0.285f * amount;
  matrix[7] = 1.f - (matrix[5] + matrix[6]);
  matrix[8] = 0;
  matrix[9] = 200.f;
  matrix[10] = 0.213f - 0.213f * amount;
  matrix[11] = 0.715f - 0.715f * amount;
  matrix[12] = 1.f - (matrix[10] + matrix[11]);
  matrix[13] = 0;
  matrix[14] = 1.5f;
  matrix[15] = matrix[16] = matrix[17] = matrix[19] = 0;
  matrix[18] = 1;
  skia::RefPtr<SkColorFilter> colorFilter(skia::AdoptRef(
      new SkColorMatrixFilter(matrix)));
  skia::RefPtr<SkImageFilter> filter =
      skia::AdoptRef(SkColorFilterImageFilter::Create(colorFilter.get(), NULL));

  scoped_ptr<RenderPassDrawQuad> render_pass_quad =
      RenderPassDrawQuad::Create();
  render_pass_quad->SetNew(pass_shared_state.get(),
                           pass_rect,
                           child_pass_id,
                           false,
                           0,
                           pass_rect,
                           gfx::RectF(),
                           WebKit::WebFilterOperations(),
                           filter,
                           WebKit::WebFilterOperations());

  root_pass->quad_list.push_back(render_pass_quad.PassAs<DrawQuad>());
  RenderPassList pass_list;

  pass_list.push_back(child_pass.Pass());
  pass_list.push_back(root_pass.Pass());

  // This test has alpha=254 for the software renderer vs. alpha=255 for the gl
  // renderer so use a fuzzy comparator.
  EXPECT_TRUE(this->RunPixelTest(
      &pass_list,
      base::FilePath(FILE_PATH_LITERAL("blue_yellow_alpha_translate.png")),
      FuzzyForSoftwareOnlyPixelComparator<TypeParam>(false)));
}

TYPED_TEST(RendererPixelTest, RenderPassChangesSize) {
  gfx::Rect viewport_rect(this->device_viewport_size_);

  RenderPass::Id root_pass_id(1, 1);
  scoped_ptr<RenderPass> root_pass =
      CreateTestRootRenderPass(root_pass_id, viewport_rect);

  RenderPass::Id child_pass_id(2, 2);
  gfx::Rect pass_rect(this->device_viewport_size_);
  gfx::Transform transform_to_root;
  scoped_ptr<RenderPass> child_pass =
      CreateTestRenderPass(child_pass_id, pass_rect, transform_to_root);

  gfx::Transform content_to_target_transform;
  scoped_ptr<SharedQuadState> shared_state =
      CreateTestSharedQuadState(content_to_target_transform, viewport_rect);

  scoped_ptr<SolidColorDrawQuad> blue = SolidColorDrawQuad::Create();
  blue->SetNew(shared_state.get(),
               gfx::Rect(0,
                         0,
                         this->device_viewport_size_.width() / 2,
                         this->device_viewport_size_.height()),
               SK_ColorBLUE);
  scoped_ptr<SolidColorDrawQuad> yellow = SolidColorDrawQuad::Create();
  yellow->SetNew(shared_state.get(),
                 gfx::Rect(this->device_viewport_size_.width() / 2,
                           0,
                           this->device_viewport_size_.width() / 2,
                           this->device_viewport_size_.height()),
                 SK_ColorYELLOW);

  child_pass->quad_list.push_back(blue.PassAs<DrawQuad>());
  child_pass->quad_list.push_back(yellow.PassAs<DrawQuad>());

  scoped_ptr<SharedQuadState> pass_shared_state =
      CreateTestSharedQuadState(gfx::Transform(), pass_rect);
  root_pass->quad_list.push_back(
      CreateTestRenderPassDrawQuad(pass_shared_state.get(),
                                   pass_rect,
                                   child_pass_id));

  RenderPassList pass_list;
  pass_list.push_back(child_pass.Pass());
  pass_list.push_back(root_pass.Pass());

  this->renderer_->SetEnlargePassTextureAmountForTesting(gfx::Vector2d(50, 75));

  EXPECT_TRUE(this->RunPixelTest(
      &pass_list,
      base::FilePath(FILE_PATH_LITERAL("blue_yellow.png")),
      ExactPixelComparator(true)));
}

template <typename RendererType>
class RendererPixelTestWithBackgroundFilter
    : public RendererPixelTest<RendererType> {
 protected:
  void SetUpRenderPassList() {
    gfx::Rect device_viewport_rect(this->device_viewport_size_);

    RenderPass::Id root_id(1, 1);
    scoped_ptr<RenderPass> root_pass =
        CreateTestRootRenderPass(root_id, device_viewport_rect);
    root_pass->has_transparent_background = false;

    gfx::Transform identity_content_to_target_transform;

    RenderPass::Id filter_pass_id(2, 1);
    gfx::Transform transform_to_root;
    scoped_ptr<RenderPass> filter_pass =
        CreateTestRenderPass(filter_pass_id,
                             filter_pass_content_rect_,
                             transform_to_root);

    // A non-visible quad in the filtering render pass.
    {
      scoped_ptr<SharedQuadState> shared_state =
          CreateTestSharedQuadState(identity_content_to_target_transform,
                                    filter_pass_content_rect_);
      scoped_ptr<SolidColorDrawQuad> color_quad = SolidColorDrawQuad::Create();
      color_quad->SetNew(shared_state.get(),
                         filter_pass_content_rect_,
                         SK_ColorTRANSPARENT);
      filter_pass->quad_list.push_back(color_quad.PassAs<DrawQuad>());
      filter_pass->shared_quad_state_list.push_back(shared_state.Pass());
    }

    {
      scoped_ptr<SharedQuadState> shared_state =
          CreateTestSharedQuadState(filter_pass_to_target_transform_,
                                    filter_pass_content_rect_);
      scoped_ptr<RenderPassDrawQuad> filter_pass_quad =
          RenderPassDrawQuad::Create();
      filter_pass_quad->SetNew(
          shared_state.get(),
          filter_pass_content_rect_,
          filter_pass_id,
          false,  // is_replica
          0,  // mask_resource_id
          filter_pass_content_rect_,  // contents_changed_since_last_frame
          gfx::RectF(),  // mask_uv_rect
          WebKit::WebFilterOperations(),  // filters
          skia::RefPtr<SkImageFilter>(),  // filter
          this->background_filters_);
      root_pass->quad_list.push_back(filter_pass_quad.PassAs<DrawQuad>());
      root_pass->shared_quad_state_list.push_back(shared_state.Pass());
    }

    const int kColumnWidth = device_viewport_rect.width() / 3;

    gfx::Rect left_rect = gfx::Rect(0, 0, kColumnWidth, 20);
    for (int i = 0; left_rect.y() < device_viewport_rect.height(); ++i) {
      scoped_ptr<SharedQuadState> shared_state =
          CreateTestSharedQuadState(identity_content_to_target_transform,
                                    left_rect);
      scoped_ptr<SolidColorDrawQuad> color_quad = SolidColorDrawQuad::Create();
      color_quad->SetNew(shared_state.get(), left_rect, SK_ColorGREEN);
      root_pass->quad_list.push_back(color_quad.PassAs<DrawQuad>());
      root_pass->shared_quad_state_list.push_back(shared_state.Pass());
      left_rect += gfx::Vector2d(0, left_rect.height() + 1);
    }

    gfx::Rect middle_rect = gfx::Rect(kColumnWidth+1, 0, kColumnWidth, 20);
    for (int i = 0; middle_rect.y() < device_viewport_rect.height(); ++i) {
      scoped_ptr<SharedQuadState> shared_state =
          CreateTestSharedQuadState(identity_content_to_target_transform,
                                    middle_rect);
      scoped_ptr<SolidColorDrawQuad> color_quad = SolidColorDrawQuad::Create();
      color_quad->SetNew(shared_state.get(), middle_rect, SK_ColorRED);
      root_pass->quad_list.push_back(color_quad.PassAs<DrawQuad>());
      root_pass->shared_quad_state_list.push_back(shared_state.Pass());
      middle_rect += gfx::Vector2d(0, middle_rect.height() + 1);
    }

    gfx::Rect right_rect = gfx::Rect((kColumnWidth+1)*2, 0, kColumnWidth, 20);
    for (int i = 0; right_rect.y() < device_viewport_rect.height(); ++i) {
      scoped_ptr<SharedQuadState> shared_state =
          CreateTestSharedQuadState(identity_content_to_target_transform,
                                    right_rect);
      scoped_ptr<SolidColorDrawQuad> color_quad = SolidColorDrawQuad::Create();
      color_quad->SetNew(shared_state.get(), right_rect, SK_ColorBLUE);
      root_pass->quad_list.push_back(color_quad.PassAs<DrawQuad>());
      root_pass->shared_quad_state_list.push_back(shared_state.Pass());
      right_rect += gfx::Vector2d(0, right_rect.height() + 1);
    }

    scoped_ptr<SharedQuadState> shared_state =
        CreateTestSharedQuadState(identity_content_to_target_transform,
                                  device_viewport_rect);
    scoped_ptr<SolidColorDrawQuad> background_quad =
        SolidColorDrawQuad::Create();
    background_quad->SetNew(shared_state.get(),
                            device_viewport_rect,
                            SK_ColorWHITE);
    root_pass->quad_list.push_back(background_quad.PassAs<DrawQuad>());
    root_pass->shared_quad_state_list.push_back(shared_state.Pass());

    pass_list_.push_back(filter_pass.Pass());
    pass_list_.push_back(root_pass.Pass());
  }

  RenderPassList pass_list_;
  WebKit::WebFilterOperations background_filters_;
  gfx::Transform filter_pass_to_target_transform_;
  gfx::Rect filter_pass_content_rect_;
};

typedef ::testing::Types<GLRenderer, SoftwareRenderer> RendererTypes;
TYPED_TEST_CASE(RendererPixelTestWithBackgroundFilter, RendererTypes);

typedef RendererPixelTestWithBackgroundFilter<GLRenderer>
GLRendererPixelTestWithBackgroundFilter;

// TODO(skaslev): The software renderer does not support filters yet.
TEST_F(GLRendererPixelTestWithBackgroundFilter, InvertFilter) {
  this->background_filters_.append(
      WebKit::WebFilterOperation::createInvertFilter(1.f));

  this->filter_pass_content_rect_ = gfx::Rect(this->device_viewport_size_);
  this->filter_pass_content_rect_.Inset(12, 14, 16, 18);

  this->SetUpRenderPassList();
  EXPECT_TRUE(this->RunPixelTest(
      &this->pass_list_,
      base::FilePath(FILE_PATH_LITERAL("background_filter.png")),
      ExactPixelComparator(true)));
}

// Software renderer does not support anti-aliased edges.
TEST_F(GLRendererPixelTest, AntiAliasing) {
  gfx::Rect rect(0, 0, 200, 200);

  RenderPass::Id id(1, 1);
  scoped_ptr<RenderPass> pass = CreateTestRootRenderPass(id, rect);

  gfx::Transform red_content_to_target_transform;
  red_content_to_target_transform.Rotate(10);
  scoped_ptr<SharedQuadState> red_shared_state =
      CreateTestSharedQuadState(red_content_to_target_transform, rect);

  scoped_ptr<SolidColorDrawQuad> red = SolidColorDrawQuad::Create();
  red->SetNew(red_shared_state.get(), rect, SK_ColorRED);

  pass->quad_list.push_back(red.PassAs<DrawQuad>());

  gfx::Transform yellow_content_to_target_transform;
  yellow_content_to_target_transform.Rotate(5);
  scoped_ptr<SharedQuadState> yellow_shared_state =
      CreateTestSharedQuadState(yellow_content_to_target_transform, rect);

  scoped_ptr<SolidColorDrawQuad> yellow = SolidColorDrawQuad::Create();
  yellow->SetNew(yellow_shared_state.get(), rect, SK_ColorYELLOW);

  pass->quad_list.push_back(yellow.PassAs<DrawQuad>());

  gfx::Transform blue_content_to_target_transform;
  scoped_ptr<SharedQuadState> blue_shared_state =
      CreateTestSharedQuadState(blue_content_to_target_transform, rect);

  scoped_ptr<SolidColorDrawQuad> blue = SolidColorDrawQuad::Create();
  blue->SetNew(blue_shared_state.get(), rect, SK_ColorBLUE);

  pass->quad_list.push_back(blue.PassAs<DrawQuad>());

  RenderPassList pass_list;
  pass_list.push_back(pass.Pass());

  EXPECT_TRUE(this->RunPixelTest(
      &pass_list,
      base::FilePath(FILE_PATH_LITERAL("anti_aliasing.png")),
      ExactPixelComparator(true)));
}

// This test tests that anti-aliasing works for axis aligned quads.
// Anti-aliasing is only supported in the gl renderer.
TEST_F(GLRendererPixelTest, AxisAligned) {
  gfx::Rect rect(0, 0, 200, 200);

  RenderPass::Id id(1, 1);
  gfx::Transform transform_to_root;
  scoped_ptr<RenderPass> pass =
      CreateTestRenderPass(id, rect, transform_to_root);

  gfx::Transform red_content_to_target_transform;
  red_content_to_target_transform.Translate(50, 50);
  red_content_to_target_transform.Scale(
      0.5f + 1.0f / (rect.width() * 2.0f),
      0.5f + 1.0f / (rect.height() * 2.0f));
  scoped_ptr<SharedQuadState> red_shared_state =
      CreateTestSharedQuadState(red_content_to_target_transform, rect);

  scoped_ptr<SolidColorDrawQuad> red = SolidColorDrawQuad::Create();
  red->SetNew(red_shared_state.get(), rect, SK_ColorRED);

  pass->quad_list.push_back(red.PassAs<DrawQuad>());

  gfx::Transform yellow_content_to_target_transform;
  yellow_content_to_target_transform.Translate(25.5f, 25.5f);
  yellow_content_to_target_transform.Scale(0.5f, 0.5f);
  scoped_ptr<SharedQuadState> yellow_shared_state =
      CreateTestSharedQuadState(yellow_content_to_target_transform, rect);

  scoped_ptr<SolidColorDrawQuad> yellow = SolidColorDrawQuad::Create();
  yellow->SetNew(yellow_shared_state.get(), rect, SK_ColorYELLOW);

  pass->quad_list.push_back(yellow.PassAs<DrawQuad>());

  gfx::Transform blue_content_to_target_transform;
  scoped_ptr<SharedQuadState> blue_shared_state =
      CreateTestSharedQuadState(blue_content_to_target_transform, rect);

  scoped_ptr<SolidColorDrawQuad> blue = SolidColorDrawQuad::Create();
  blue->SetNew(blue_shared_state.get(), rect, SK_ColorBLUE);

  pass->quad_list.push_back(blue.PassAs<DrawQuad>());

  RenderPassList pass_list;
  pass_list.push_back(pass.Pass());

  EXPECT_TRUE(this->RunPixelTest(
      &pass_list,
      base::FilePath(FILE_PATH_LITERAL("axis_aligned.png")),
      ExactPixelComparator(true)));
}

static void SyncPointCallback(int* callback_count) {
  ++(*callback_count);
  base::MessageLoop::current()->QuitWhenIdle();
}

static void OtherCallback(int* callback_count) {
  ++(*callback_count);
  base::MessageLoop::current()->QuitWhenIdle();
}

TEST_F(GLRendererPixelTest, SignalSyncPointOnLostContext) {
  int sync_point_callback_count = 0;
  int other_callback_count = 0;
  unsigned sync_point = output_surface_->context3d()->insertSyncPoint();

  output_surface_->context3d()->loseContextCHROMIUM(
      GL_GUILTY_CONTEXT_RESET_ARB, GL_INNOCENT_CONTEXT_RESET_ARB);

  SyncPointHelper::SignalSyncPoint(
      output_surface_->context3d(),
      sync_point,
      base::Bind(&SyncPointCallback, &sync_point_callback_count));
  EXPECT_EQ(0, sync_point_callback_count);
  EXPECT_EQ(0, other_callback_count);

  // Make the sync point happen.
  output_surface_->context3d()->finish();
  // Post a task after the sync point.
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&OtherCallback, &other_callback_count));

  base::MessageLoop::current()->Run();

  // The sync point shouldn't have happened since the context was lost.
  EXPECT_EQ(0, sync_point_callback_count);
  EXPECT_EQ(1, other_callback_count);
}

TEST_F(GLRendererPixelTest, SignalSyncPoint) {
  int sync_point_callback_count = 0;
  int other_callback_count = 0;
  unsigned sync_point = output_surface_->context3d()->insertSyncPoint();

  SyncPointHelper::SignalSyncPoint(
      output_surface_->context3d(),
      sync_point,
      base::Bind(&SyncPointCallback, &sync_point_callback_count));
  EXPECT_EQ(0, sync_point_callback_count);
  EXPECT_EQ(0, other_callback_count);

  // Make the sync point happen.
  output_surface_->context3d()->finish();
  // Post a task after the sync point.
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&OtherCallback, &other_callback_count));

  base::MessageLoop::current()->Run();

  // The sync point should have happened.
  EXPECT_EQ(1, sync_point_callback_count);
  EXPECT_EQ(1, other_callback_count);
}
#endif

}  // namespace
}  // namespace cc