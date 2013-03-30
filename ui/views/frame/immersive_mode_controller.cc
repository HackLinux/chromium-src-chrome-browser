// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/immersive_mode_controller.h"

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/common/chrome_switches.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/screen.h"
#include "ui/gfx/transform.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

#if defined(USE_ASH)
#include "ash/ash_switches.h"
#include "ash/shell.h"
#include "ash/wm/window_properties.h"
#include "base/command_line.h"
#endif

#if defined(USE_AURA)
#include "ui/aura/client/activation_client.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/capture_client.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#endif

using views::View;

namespace {

// Time after which the edge trigger fires and top-chrome is revealed. This is
// after the mouse stops moving.
const int kTopEdgeRevealDelayMs = 200;

// Duration for the reveal show/hide slide animation. The slower duration is
// used for the initial slide out to give the user more change to see what
// happened.
const int kRevealSlowAnimationDurationMs = 400;
const int kRevealFastAnimationDurationMs = 200;

// Returns true if the currently active window is a transient child of
// |toplevel|.
bool IsActiveWindowTransientChildOf(gfx::NativeWindow toplevel) {
#if defined(USE_AURA)
  // TODO(pkotwicz): Porting to non-aura will require a way of getting
  // the currently active native window and its relationship to |toplevel|
  // which is not yet provided by views::Widget.
  aura::Window* active_window = aura::client::GetActivationClient(
      toplevel->GetRootWindow())->GetActiveWindow();

  if (!toplevel || !active_window)
    return false;

  for (aura::Window* window = active_window; window;
       window = window->transient_parent()) {
    if (window == toplevel)
      return true;
  }
#endif  // defined(USE_AURA)
  return false;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////

ImmersiveModeController::RevealedLock::RevealedLock(
    const base::WeakPtr<ImmersiveModeController>& controller)
    : controller_(controller) {
  DCHECK(controller_);
  controller_->LockRevealedState();
}

ImmersiveModeController::RevealedLock::~RevealedLock() {
  if (controller_)
    controller_->UnlockRevealedState();
}

////////////////////////////////////////////////////////////////////////////////

#if defined(USE_AURA)
// Observer to watch for window restore. views::Widget does not provide a hook
// to observe for window restore, so do this at the Aura level.
class ImmersiveModeController::WindowObserver : public aura::WindowObserver {
 public:
  explicit WindowObserver(ImmersiveModeController* controller)
      : controller_(controller) {
    controller_->native_window_->AddObserver(this);
  }

  virtual ~WindowObserver() {
    controller_->native_window_->RemoveObserver(this);
  }

  // aura::WindowObserver overrides:
  virtual void OnWindowPropertyChanged(aura::Window* window,
                                       const void* key,
                                       intptr_t old) OVERRIDE {
    using aura::client::kShowStateKey;
    if (key == kShowStateKey) {
      // Disable immersive mode when leaving the fullscreen state.
      if (window->GetProperty(kShowStateKey) != ui::SHOW_STATE_FULLSCREEN)
        controller_->SetEnabled(false);
      return;
    }
#if defined(USE_ASH)
    using ash::internal::kImmersiveModeKey;
    if (key == kImmersiveModeKey) {
      // Another component has toggled immersive mode.
      controller_->SetEnabled(window->GetProperty(kImmersiveModeKey));
      return;
    }
#endif
  }

 private:
  ImmersiveModeController* controller_;  // Not owned.

  DISALLOW_COPY_AND_ASSIGN(WindowObserver);
};
#endif  // defined(USE_AURA)

////////////////////////////////////////////////////////////////////////////////

class ImmersiveModeController::AnimationObserver
    : public ui::ImplicitAnimationObserver {
 public:
  enum AnimationType {
    SLIDE_OPEN,
    SLIDE_CLOSED,
  };

  AnimationObserver(ImmersiveModeController* controller, AnimationType type)
      : controller_(controller), animation_type_(type) {}
  virtual ~AnimationObserver() {}

  // ui::ImplicitAnimationObserver overrides:
  virtual void OnImplicitAnimationsCompleted() OVERRIDE {
    if (animation_type_ == SLIDE_OPEN)
      controller_->OnSlideOpenAnimationCompleted();
    else if (animation_type_ == SLIDE_CLOSED)
      controller_->OnSlideClosedAnimationCompleted();
    else
      NOTREACHED();
  }

 private:
  ImmersiveModeController* controller_;
  AnimationType animation_type_;

  DISALLOW_COPY_AND_ASSIGN(AnimationObserver);
};

////////////////////////////////////////////////////////////////////////////////

ImmersiveModeController::ImmersiveModeController()
    : browser_view_(NULL),
      enabled_(false),
      reveal_state_(CLOSED),
      revealed_lock_count_(0),
      hide_tab_indicators_(false),
      native_window_(NULL),
      weak_ptr_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
}

ImmersiveModeController::~ImmersiveModeController() {
  // The browser view is being destroyed so there's no need to update its
  // layout or layers, even if the top views are revealed. But the window
  // observers still need to be removed.
  EnableWindowObservers(false);
}

void ImmersiveModeController::Init(BrowserView* browser_view) {
  browser_view_ = browser_view;
  // Browser view is detached from its widget during destruction. Cache the
  // window pointer so |this| can stop observing during destruction.
  native_window_ = browser_view_->GetNativeWindow();
  DCHECK(native_window_);
  EnableWindowObservers(true);

  slide_open_observer_.reset(
      new AnimationObserver(this, AnimationObserver::SLIDE_OPEN));
  slide_closed_observer_.reset(
      new AnimationObserver(this, AnimationObserver::SLIDE_CLOSED));

#if defined(USE_ASH)
  // Optionally allow the tab indicators to be hidden.
  hide_tab_indicators_ = CommandLine::ForCurrentProcess()->
      HasSwitch(ash::switches::kAshImmersiveHideTabIndicators);
#endif
}

// static
bool ImmersiveModeController::UseImmersiveFullscreen() {
#if defined(OS_CHROMEOS)
  // Kiosk mode needs the whole screen.
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  return !command_line->HasSwitch(switches::kKioskMode) &&
      command_line->HasSwitch(ash::switches::kAshImmersiveFullscreen);
#endif
  return false;
}

void ImmersiveModeController::SetEnabled(bool enabled) {
  DCHECK(browser_view_) << "Must initialize before enabling";
  if (enabled_ == enabled)
    return;
  enabled_ = enabled;

  TopContainerView* top_container = browser_view_->top_container();
  if (enabled_) {
    // Animate enabling immersive mode by sliding out the top-of-window views.
    // No animation occurs if a lock is holding the top-of-window views open.

    // Do a reveal to set the initial state for the animation. (And any
    // required state in case the animation cannot run because of a lock holding
    // the top-of-window views open.)
    StartReveal(ANIMATE_NO);

    // Reset the mouse and the focus revealed locks so that they do not affect
    // whether the top-of-window views are hidden. Reacquire the locks if ending
    // the reveal is unsuccessful.
    bool had_mouse_revealed_lock = (mouse_revealed_lock_.get() != NULL);
    bool had_focus_revealed_lock = (focus_revealed_lock_.get() != NULL);
    mouse_revealed_lock_.reset();
    focus_revealed_lock_.reset();

    // Try doing the animation.
    MaybeEndReveal(ANIMATE_SLOW);

    if (IsRevealed()) {
      if (had_mouse_revealed_lock)
        mouse_revealed_lock_.reset(GetRevealedLock());
      if (had_focus_revealed_lock)
        focus_revealed_lock_.reset(GetRevealedLock());
    }
  } else {
    // Stop cursor-at-top tracking.
    top_timer_.Stop();
    // Snap immediately to the closed state.
    reveal_state_ = CLOSED;
    top_container->SetFillsBoundsOpaquely(false);
    top_container->SetPaintToLayer(false);
    browser_view_->GetWidget()->non_client_view()->frame_view()->
        ResetWindowControls();
    browser_view_->tabstrip()->SetImmersiveStyle(false);
  }
  // Don't need explicit layout because we're inside a fullscreen transition
  // and it blocks layout calls.

#if defined(USE_ASH)
  // This causes a no-op call to SetEnabled() since enabled_ is already set.
  native_window_->SetProperty(ash::internal::kImmersiveModeKey, enabled_);
  // Ash on Windows may not have a shell.
  if (ash::Shell::HasInstance()) {
    // Shelf auto-hides in immersive mode.
    ash::Shell::GetInstance()->UpdateShelfVisibility();
  }
#endif
}

void ImmersiveModeController::MaybeStackViewAtTop() {
#if defined(USE_AURA)
  if (enabled_ && reveal_state_ != CLOSED) {
    ui::Layer* reveal_layer = browser_view_->top_container()->layer();
    if (reveal_layer)
      reveal_layer->parent()->StackAtTop(reveal_layer);
  }
#endif
}

void ImmersiveModeController::MaybeStartReveal() {
  if (enabled_ && reveal_state_ != REVEALED)
    StartReveal(ANIMATE_FAST);
}

void ImmersiveModeController::CancelReveal() {
  // Reset the mouse revealed lock so that it does not affect whether
  // the top-of-window views are hidden. Reaquire the lock if ending the reveal
  // is unsuccessful.
  bool had_mouse_revealed_lock = (mouse_revealed_lock_.get() != NULL);
  mouse_revealed_lock_.reset();
  MaybeEndReveal(ANIMATE_NO);
  if (IsRevealed() && had_mouse_revealed_lock)
    mouse_revealed_lock_.reset(GetRevealedLock());
}

ImmersiveModeController::RevealedLock*
    ImmersiveModeController::GetRevealedLock() {
  return new RevealedLock(weak_ptr_factory_.GetWeakPtr());
}

////////////////////////////////////////////////////////////////////////////////
// Observers:

void ImmersiveModeController::OnMouseEvent(ui::MouseEvent* event) {
  if (!enabled_)
    return;

  if (event->flags() & ui::EF_IS_SYNTHESIZED)
    return;

  // Handle ET_MOUSE_PRESSED and ET_MOUSE_RELEASED so that we get the updated
  // mouse position ASAP once a nested message loop finishes running.
  if (event->type() != ui::ET_MOUSE_MOVED &&
      event->type() != ui::ET_MOUSE_PRESSED &&
      event->type() != ui::ET_MOUSE_RELEASED) {
    return;
  }

  // Mouse hover should not initiate revealing the top-of-window views while
  // |native_window_| is inactive.
  if (!views::Widget::GetWidgetForNativeWindow(native_window_)->IsActive())
    return;

  if ((reveal_state_ == SLIDING_CLOSED || reveal_state_ == CLOSED) &&
      event->root_location().y() == 0) {
    // Start a reveal if the mouse touches the top of the screen and then stops
    // moving for a little while. This mirrors the Ash launcher behavior.
    top_timer_.Stop();
    // Timer is stopped when |this| is destroyed, hence Unretained() is safe.
    top_timer_.Start(
        FROM_HERE,
        base::TimeDelta::FromMilliseconds(kTopEdgeRevealDelayMs),
        base::Bind(&ImmersiveModeController::AcquireMouseRevealedLock,
                   base::Unretained(this)));
  } else {
    // Cursor left the top edge or the top-of-window views are already revealed.
    top_timer_.Stop();
  }

  UpdateMouseRevealedLock(false);
  // Pass along event for further handling.
}

void ImmersiveModeController::OnWillChangeFocus(views::View* focused_before,
                                                views::View* focused_now) {
}

void ImmersiveModeController::OnDidChangeFocus(views::View* focused_before,
                                               views::View* focused_now) {
  UpdateFocusRevealedLock();
}

void ImmersiveModeController::OnWidgetDestroying(views::Widget* widget) {
  EnableWindowObservers(false);
  native_window_ = NULL;

  // Set |enabled_| to false such that any calls to MaybeStartReveal() and
  // MaybeEndReveal() have no effect.
  enabled_ = false;
}

void ImmersiveModeController::OnWidgetActivationChanged(views::Widget* widget,
                                                        bool active) {
  // Mouse hover should not initiate revealing the top-of-window views while
  // |native_window_| is inactive.
  top_timer_.Stop();

  UpdateMouseRevealedLock(true);
  UpdateFocusRevealedLock();
}

////////////////////////////////////////////////////////////////////////////////
// Testing interface:

void ImmersiveModeController::SetHideTabIndicatorsForTest(bool hide) {
  hide_tab_indicators_ = hide;
}

void ImmersiveModeController::StartRevealForTest(bool hovered) {
  StartReveal(ANIMATE_NO);
  SetMouseHoveredForTest(hovered);
}

void ImmersiveModeController::SetMouseHoveredForTest(bool hovered) {
#if defined(USE_AURA)
  // TODO(pkotwicz): Porting to non-Aura will require a better way of
  // setting the mouse hovered state.
  views::View* top_container = browser_view_->top_container();
  gfx::Point cursor_pos;
  if (!hovered) {
    int bottom_edge = top_container->bounds().bottom();
    cursor_pos = gfx::Point(0, bottom_edge + 100);
  }
  views::View::ConvertPointToScreen(top_container, &cursor_pos);
  aura::Env::GetInstance()->set_last_mouse_location(cursor_pos);
#endif  // defined(USE_AURA)

  UpdateMouseRevealedLock(true);
}

////////////////////////////////////////////////////////////////////////////////
// private:

void ImmersiveModeController::EnableWindowObservers(bool enable) {
  if (!native_window_) {
    DCHECK(!enable) << "ImmersiveModeController not initialized";
    return;
  }

  views::Widget* widget =
      views::Widget::GetWidgetForNativeWindow(native_window_);
  views::FocusManager* focus_manager = widget->GetFocusManager();
  if (enable) {
    widget->AddObserver(this);
    focus_manager->AddFocusChangeListener(this);
  } else {
    widget->RemoveObserver(this);
    focus_manager->RemoveFocusChangeListener(this);
  }

#if defined(USE_AURA)
  // TODO(jamescook): Porting immersive mode to non-Aura views will require
  // a method to monitor incoming mouse move events without handling them.
  // Currently views uses GetEventHandlerForPoint() to route events directly
  // to either a tab or the caption area, bypassing pre-target handlers and
  // intermediate views.
  if (enable)
    native_window_->AddPreTargetHandler(this);
  else
    native_window_->RemovePreTargetHandler(this);

  // The window observer adds and removes itself from the native window.
  // TODO(jamescook): Porting to non-Aura will also require a method to monitor
  // for window restore, which is not provided by views Widget.
  window_observer_.reset(enable ? new WindowObserver(this) : NULL);
#endif  // defined(USE_AURA)
}

void ImmersiveModeController::UpdateMouseRevealedLock(bool maybe_drag) {
  if (!enabled_)
    return;

  // Hover cannot initiate a reveal when the top-of-window views are sliding
  // closed or are closed. (With the exception of hovering at y = 0 which is
  // handled in OnMouseEvent() ).
  if (reveal_state_ == SLIDING_CLOSED || reveal_state_ == CLOSED)
    return;

  // Mouse hover should not keep the top-of-window views revealed if
  // |native_window_| is not active.
  if (!views::Widget::GetWidgetForNativeWindow(native_window_)->IsActive()) {
    mouse_revealed_lock_.reset();
    return;
  }

#if defined(USE_AURA)
  // TODO(pkotwicz): Porting to non-Aura will require a way of getting the
  // widget which currently has mouse capture which is not yet provided by
  // views::Widget.
  // If a window has capture, we may be in the middle of a drag. Delay updating
  // the revealed lock till we get more specifics via OnMouseEvent().
  if (maybe_drag && aura::client::GetCaptureWindow(native_window_))
    return;
#endif  // defined(USE_AURA)

  views::View* top_container = browser_view_->top_container();
  gfx::Point cursor_pos = gfx::Screen::GetScreenFor(
      native_window_)->GetCursorScreenPoint();
  views::View::ConvertPointFromScreen(top_container, &cursor_pos);

  if (top_container->bounds().Contains(cursor_pos))
    AcquireMouseRevealedLock();
  else
    mouse_revealed_lock_.reset();
}

void ImmersiveModeController::AcquireMouseRevealedLock() {
  if (!mouse_revealed_lock_.get())
    mouse_revealed_lock_.reset(GetRevealedLock());
}

void ImmersiveModeController::UpdateFocusRevealedLock() {
  if (!enabled_)
    return;

  bool hold_lock = false;
  views::Widget* widget =
      views::Widget::GetWidgetForNativeWindow(native_window_);
  if (widget->IsActive()) {
    views::View* focused_view = widget->GetFocusManager()->GetFocusedView();
    if (browser_view_->top_container()->Contains(focused_view))
      hold_lock = true;
  } else {
    // If the currently active window is not |native_window_|, the top-of-window
    // views should be revealed if:
    // 1) The newly active window is a transient child of |native_window_|.
    // 2) The top-of-window views are already revealed. This restriction
    //    prevents a transient window opened by the web contents while the
    //    top-of-window views are hidden from from initiating a reveal.
    // The top-of-window views will stay revealed till |native_window_| is
    // reactivated.
    if (IsRevealed() && IsActiveWindowTransientChildOf(native_window_))
      hold_lock = true;
  }

  if (hold_lock) {
    if (!focus_revealed_lock_.get())
      focus_revealed_lock_.reset(GetRevealedLock());
  } else {
    focus_revealed_lock_.reset();
  }
}

void ImmersiveModeController::LockRevealedState() {
  ++revealed_lock_count_;
  if (revealed_lock_count_ == 1)
    MaybeStartReveal();
}

void ImmersiveModeController::UnlockRevealedState() {
  --revealed_lock_count_;
  DCHECK_GE(revealed_lock_count_, 0);
  if (revealed_lock_count_ == 0)
    MaybeEndReveal(ANIMATE_FAST);
}

int ImmersiveModeController::GetAnimationDuration(Animate animate) const {
  switch (animate) {
    case ANIMATE_NO:
      return 0;
    case ANIMATE_SLOW:
      return kRevealSlowAnimationDurationMs;
    case ANIMATE_FAST:
      return kRevealFastAnimationDurationMs;
  }
  NOTREACHED();
  return 0;
}

void ImmersiveModeController::StartReveal(Animate animate) {
  if (reveal_state_ == CLOSED) {
    reveal_state_ = SLIDING_OPEN;
    // Turn on layer painting so we can smoothly animate.
    TopContainerView* top_container = browser_view_->top_container();
    top_container->SetPaintToLayer(true);
    top_container->SetFillsBoundsOpaquely(true);

    // Ensure window caption buttons are updated and the view bounds are
    // computed at normal (non-immersive-style) size.
    LayoutBrowserView(false);

    if (animate != ANIMATE_NO) {
      // Now that we have a layer, move it to the initial offscreen position.
      ui::Layer* layer = top_container->layer();
      gfx::Transform transform;
      transform.Translate(0, -layer->bounds().height());
      layer->SetTransform(transform);
    }
    // Slide in the reveal view.
    AnimateSlideOpen(GetAnimationDuration(animate));
  } else if (reveal_state_ == SLIDING_CLOSED) {
    reveal_state_ = SLIDING_OPEN;
    // Reverse the animation.
    AnimateSlideOpen(GetAnimationDuration(animate));
  }
}

void ImmersiveModeController::LayoutBrowserView(bool immersive_style) {
  // Update the window caption buttons.
  browser_view_->GetWidget()->non_client_view()->frame_view()->
      ResetWindowControls();
  browser_view_->tabstrip()->SetImmersiveStyle(immersive_style);
  browser_view_->Layout();
}

void ImmersiveModeController::AnimateSlideOpen(int duration_ms) {
  ui::Layer* layer = browser_view_->top_container()->layer();
  // Stop any slide closed animation in progress.
  layer->GetAnimator()->AbortAllAnimations();

  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.AddObserver(slide_open_observer_.get());
  settings.SetTweenType(ui::Tween::EASE_OUT);
  settings.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(duration_ms));
  layer->SetTransform(gfx::Transform());
}

void ImmersiveModeController::OnSlideOpenAnimationCompleted() {
  if (reveal_state_ == SLIDING_OPEN) {
    reveal_state_ = REVEALED;

    // The user may not have moved the mouse since the reveal was initiated.
    // Update the revealed lock to reflect the mouse's current state.
    UpdateMouseRevealedLock(true);
  }
}

void ImmersiveModeController::MaybeEndReveal(Animate animate) {
  if (enabled_ && reveal_state_ != CLOSED && revealed_lock_count_ == 0)
    EndReveal(animate);
}

void ImmersiveModeController::EndReveal(Animate animate) {
  if (reveal_state_ == SLIDING_OPEN || reveal_state_ == REVEALED) {
    reveal_state_ = SLIDING_CLOSED;
    int duration_ms = GetAnimationDuration(animate);
    if (duration_ms > 0)
      AnimateSlideClosed(duration_ms);
    else
      OnSlideClosedAnimationCompleted();
  }
}

void ImmersiveModeController::AnimateSlideClosed(int duration_ms) {
  // Stop any slide open animation in progress, but don't skip to the end. This
  // avoids a visual "pop" when starting a hide in the middle of a show.
  ui::Layer* layer = browser_view_->top_container()->layer();
  layer->GetAnimator()->AbortAllAnimations();

  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetTweenType(ui::Tween::EASE_OUT);
  settings.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(duration_ms));
  settings.AddObserver(slide_closed_observer_.get());
  gfx::Transform transform;
  transform.Translate(0, -layer->bounds().height());
  layer->SetTransform(transform);
}

void ImmersiveModeController::OnSlideClosedAnimationCompleted() {
  if (reveal_state_ == SLIDING_CLOSED) {
    reveal_state_ = CLOSED;
    TopContainerView* top_container = browser_view_->top_container();
    // Layer isn't needed after animation completes.
    top_container->SetFillsBoundsOpaquely(false);
    top_container->SetPaintToLayer(false);
    // Update tabstrip for closed state.
    LayoutBrowserView(true);
  }
}
