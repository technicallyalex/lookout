// SPDX-License-Identifier: GPL-2.0-or-later
#include "backdrop.hpp"

#include <dispatcherqueue.h>
#include <d2d1effects.h>
#include <dwmapi.h>
#include <windows.graphics.effects.interop.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>

using namespace winrt::Windows::UI::Composition;
namespace effects=winrt::Windows::Graphics::Effects;
namespace nativeEffects=ABI::Windows::Graphics::Effects;

namespace {
// The compositor consumes a Direct2D effect descriptor. No desktop pixels
// enter the process, and no Win2D runtime has to be installed.
struct GaussianBlur:winrt::implements<GaussianBlur,effects::IGraphicsEffect,
 effects::IGraphicsEffectSource,nativeEffects::IGraphicsEffectD2D1Interop> {
 winrt::hstring name{L"Blur"};
 effects::IGraphicsEffectSource source{CompositionEffectSourceParameter(L"Backdrop")};

 winrt::hstring Name() const {return name;}
 void Name(const winrt::hstring &value){name=value;}
 HRESULT __stdcall GetEffectId(GUID *id) noexcept override {
  *id=CLSID_D2D1GaussianBlur;
  return S_OK;
 }
 HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR key,UINT *index,
  nativeEffects::GRAPHICS_EFFECT_PROPERTY_MAPPING *mapping) noexcept override {
  if(wcscmp(key,L"Radius")!=0)return E_INVALIDARG;
  *index=D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION;
  *mapping=nativeEffects::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
  return S_OK;
 }
 HRESULT __stdcall GetPropertyCount(UINT *count) noexcept override {*count=3;return S_OK;}
 HRESULT __stdcall GetProperty(UINT index,ABI::Windows::Foundation::IPropertyValue **out) noexcept override {
  using winrt::Windows::Foundation::PropertyValue;
  try {
   winrt::Windows::Foundation::IInspectable property;
   switch(index){
    case D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION:property=PropertyValue::CreateSingle(1.0f);break;
    case D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION:property=PropertyValue::CreateUInt32(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);break;
    case D2D1_GAUSSIANBLUR_PROP_BORDER_MODE:property=PropertyValue::CreateUInt32(D2D1_BORDER_MODE_HARD);break;
    default:return E_INVALIDARG;
   }
   *out=property.as<ABI::Windows::Foundation::IPropertyValue>().detach();
   return S_OK;
  } catch(...){return winrt::to_hresult();}
 }
 HRESULT __stdcall GetSourceCount(UINT *count) noexcept override {*count=1;return S_OK;}
 HRESULT __stdcall GetSource(UINT index,nativeEffects::IGraphicsEffectSource **out) noexcept override {
  if(index!=0)return E_INVALIDARG;
  try {*out=source.as<nativeEffects::IGraphicsEffectSource>().detach();return S_OK;}
  catch(...){return winrt::to_hresult();}
 }
};
}

struct BackdropHost::State {
 struct Visual {
  Desktop::DesktopWindowTarget target{nullptr};
  SpriteVisual sprite{nullptr};
  CompositionEffectBrush brush{nullptr};
 };
 winrt::Windows::System::DispatcherQueueController queue{nullptr};
 Compositor compositor{nullptr};
 CompositionEffectFactory factory{nullptr};
 std::unordered_map<HWND,Visual> visuals;
 bool initialized=false;
};

BackdropHost::BackdropHost():state_(std::make_unique<State>()){}
BackdropHost::~BackdropHost()=default;

bool BackdropHost::update(HWND window,int amount)
{
 try {
  if(!state_->initialized){
   state_->initialized=true;
   if(!winrt::Windows::System::DispatcherQueue::GetForCurrentThread()){
    DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_NONE};
    winrt::check_hresult(CreateDispatcherQueueController(options,
     reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(state_->queue))));
   }
   state_->compositor=Compositor();
   state_->factory=state_->compositor.CreateEffectFactory(winrt::make<GaussianBlur>(),
    winrt::single_threaded_vector<winrt::hstring>({L"Blur.Radius"}));
  }
  if(!state_->factory)return false;
  auto visual=state_->visuals.find(window);
  if(visual==state_->visuals.end()){
   const BOOL enabled=TRUE;
   if(FAILED(DwmSetWindowAttribute(window,DWMWA_USE_HOSTBACKDROPBRUSH,&enabled,sizeof(enabled))))return false;
   State::Visual layer;
   const auto interop=state_->compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
   winrt::check_hresult(interop->CreateDesktopWindowTarget(window,TRUE,
    reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(winrt::put_abi(layer.target))));
   layer.sprite=state_->compositor.CreateSpriteVisual();
   layer.sprite.RelativeSizeAdjustment({1.0f,1.0f});
   layer.brush=state_->factory.CreateBrush();
   layer.brush.SetSourceParameter(L"Backdrop",state_->compositor.CreateHostBackdropBrush());
   layer.sprite.Brush(layer.brush);
   layer.target.Root(layer.sprite);
   visual=state_->visuals.emplace(window,std::move(layer)).first;
  }
  // The helper has no painted surface. At low strength blend the softened
  // host backdrop directly over the live desktop to preserve fine detail.
  const float fraction=std::clamp(amount,0,100)/100.0f;
  const float deviation=32.0f*std::pow(fraction,2.2f);
  visual->second.brush.Properties().InsertScalar(L"Blur.Radius",deviation);
  visual->second.sprite.Opacity(fraction);
  return true;
 } catch(const winrt::hresult_error &){
  return false;
 }
}

void BackdropHost::remove(HWND window)
{
 state_->visuals.erase(window);
}
