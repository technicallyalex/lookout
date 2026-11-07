// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared-preview.hpp"
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <QSGGeometryNode>
#include <QSGTextureMaterial>
#include <QScreen>
#include <QtQuick/qsgtexture_platform.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cmath>
#include <algorithm>
using Microsoft::WRL::ComPtr;
namespace {
struct PreviewNode final:QSGGeometryNode {
 QSGGeometry mesh{QSGGeometry::defaultAttributes_TexturedPoint2D(),108};
 QSGTextureMaterial ink;
 QSGTexture *wrapped=nullptr;
 ComPtr<ID3D11Texture2D> shared,local;
 ComPtr<IDXGIKeyedMutex> mutex;
 quint32 handle=0;
 int generation=-1;
 bool ready=false;
 QSizeF size;
 PreviewNode(){mesh.setDrawingMode(QSGGeometry::DrawTriangles);setGeometry(&mesh);setMaterial(&ink);}
 ~PreviewNode(){delete wrapped;}
};
}
SharedPreview::SharedPreview()
{
 setFlag(ItemHasContents,true);
 setAntialiasing(true);
 connect(&frameNotifier_,&QWinEventNotifier::activated,this,&SharedPreview::requestFrame);
 fallbackTimer_.setTimerType(Qt::PreciseTimer);
 connect(&fallbackTimer_,&QTimer::timeout,this,&SharedPreview::requestFrame);
}

SharedPreview::~SharedPreview()
{
 closeFrameEvent();
}

void SharedPreview::closeFrameEvent()
{
 const auto handle=frameNotifier_.handle();
 if(!handle)return;
 frameNotifier_.setHandle(nullptr);
 CloseHandle(handle);
}

void SharedPreview::requestFrame()
{
 framePending_=true;
 if(isVisible() && window() && window()->isExposed())update();
}

void SharedPreview::setDescriptor(const QVariantMap &value)
{
 if(value==descriptor_)return;
 const auto eventName=value.value("frameEvent").toString();
 if(eventName!=descriptor_.value("frameEvent").toString()){
  closeFrameEvent();
  if(!eventName.isEmpty()){
   const auto event=OpenEventW(SYNCHRONIZE,FALSE,reinterpret_cast<LPCWSTR>(eventName.utf16()));
   if(event){
    frameNotifier_.setHandle(event);
    frameNotifier_.setEnabled(true);
   }
  }
 }
 descriptor_=value;
 if(frameNotifier_.handle() || !value.value("handle").toDouble()){
  fallbackTimer_.stop();
 }else if(!fallbackTimer_.isActive()){
  const auto *screen=window()?window()->screen():nullptr;
  const auto refreshRate=screen?screen->refreshRate():60.0;
  fallbackTimer_.start(std::max(1,qRound(500.0/std::max(30.0,refreshRate))));
 }
 emit descriptorChanged();
 framePending_=true;
 update();
}

QSGNode *SharedPreview::updatePaintNode(QSGNode *old,UpdatePaintNodeData*){
 auto *node=static_cast<PreviewNode*>(old);const quint32 handle=quint32(descriptor_["handle"].toDouble());if(!handle){delete node;return nullptr;}auto *device=static_cast<ID3D11Device*>(window()->rendererInterface()->getResource(window(),QSGRendererInterface::DeviceResource));if(!device){delete node;return nullptr;}
 if(!node)node=new PreviewNode;
 const int generation=descriptor_["generation"].toInt();
 if(node->generation!=generation || node->handle!=handle){delete node->wrapped;node->wrapped=nullptr;node->local.Reset();node->shared.Reset();node->mutex.Reset();node->generation=generation;node->handle=handle;node->ready=false;
  HRESULT hr=device->OpenSharedResource(reinterpret_cast<HANDLE>(quintptr(handle)),IID_PPV_ARGS(&node->shared));if(FAILED(hr) || FAILED(node->shared.As(&node->mutex))){emit importFailed(QString("GPU thumbnail import failed (0x%1)").arg(quint32(hr),8,16,QChar('0')));delete node;return nullptr;}
  D3D11_TEXTURE2D_DESC desc{};node->shared->GetDesc(&desc);desc.MiscFlags=0;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=0;desc.Usage=D3D11_USAGE_DEFAULT;
  if(FAILED(device->CreateTexture2D(&desc,nullptr,&node->local))){emit importFailed("GPU thumbnail allocation failed");delete node;return nullptr;}
  node->wrapped=QNativeInterface::QSGD3D11Texture::fromNative(node->local.Get(),window(),QSize(desc.Width,desc.Height));if(!node->wrapped){emit importFailed("GPU thumbnail wrapping failed");delete node;return nullptr;}node->ink.setTexture(node->wrapped);node->ink.setFiltering(QSGTexture::Linear);
 }
 if((framePending_ || !node->ready) && node->mutex->AcquireSync(0,0)==S_OK){
  ComPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(&context);
  context->CopyResource(node->local.Get(),node->shared.Get());
  context->Flush();
  node->mutex->ReleaseSync(0);
  framePending_=false;
  node->markDirty(QSGNode::DirtyMaterial);
  if(!node->ready){node->ready=true;emit frameCopied();}
 }
 if(!node->ready){delete node;return nullptr;}
 if(node->size==size())return node;
 node->size=size();
 // Geometry clipping keeps video corners rounded on the GPU. MSAA provides
 // smooth coverage; no QWidget mask, CPU readback or unprotected child HWND.
 const float w=float(width()),h=float(height()),r=std::min({8.f,w/2,h/2});auto *vertices=node->mesh.vertexDataAsTexturedPoint2D();int n=0;auto vertex=[&](float x,float y){vertices[n++].set(x,y,x/std::max(1.f,w),y/std::max(1.f,h));};QVector<QPointF> boundary;
 for(int corner=0;corner<4;++corner){const float cx=corner==0 || corner==3?w-r:r,cy=corner<2?h-r:r;for(int step=0;step<=8;++step){const double angle=(corner*90+step*90.0/8)*3.141592653589793/180;boundary<<QPointF(cx+r*std::cos(angle),cy+r*std::sin(angle));}}
 for(int i=0;i<boundary.size();++i){const auto a=boundary[i],b=boundary[(i+1)%boundary.size()];vertex(w/2,h/2);vertex(float(a.x()),float(a.y()));vertex(float(b.x()),float(b.y()));}node->markDirty(QSGNode::DirtyGeometry);return node;
}
