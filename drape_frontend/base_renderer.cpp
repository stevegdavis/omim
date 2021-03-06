#include "drape_frontend/base_renderer.hpp"
#include "drape_frontend/message_subclasses.hpp"

#include "std/utility.hpp"

namespace df
{

BaseRenderer::BaseRenderer(ThreadsCommutator::ThreadName name, Params const & params)
  : m_commutator(params.m_commutator)
  , m_contextFactory(params.m_oglContextFactory)
  , m_texMng(params.m_texMng)
  , m_threadName(name)
  , m_isEnabled(true)
  , m_renderingEnablingCompletionHandler(nullptr)
  , m_wasNotified(false)
  , m_wasContextReset(false)
{
  m_commutator->RegisterThread(m_threadName, this);
}

void BaseRenderer::StartThread()
{
  m_selfThread.Create(CreateRoutine());
}

void BaseRenderer::StopThread()
{
  // stop rendering and close queue
  m_selfThread.GetRoutine()->Cancel();
  CloseQueue();

  // wake up render thread if necessary
  if (!m_isEnabled)
  {
    WakeUp();
  }

  // wait for render thread completion
  m_selfThread.Join();
}

void BaseRenderer::SetRenderingEnabled(ref_ptr<dp::OGLContextFactory> contextFactory)
{
  if (m_wasContextReset && contextFactory != nullptr)
    m_contextFactory = contextFactory;
  SetRenderingEnabled(true);
}

void BaseRenderer::SetRenderingDisabled(bool const destroyContext)
{
  if (destroyContext)
    m_wasContextReset = true;
  SetRenderingEnabled(false);
}

bool BaseRenderer::IsRenderingEnabled() const
{
  return m_isEnabled;
}

void BaseRenderer::SetRenderingEnabled(bool const isEnabled)
{
  if (isEnabled == m_isEnabled)
    return;

  // here we have to wait for completion of internal SetRenderingEnabled
  mutex completionMutex;
  condition_variable completionCondition;
  bool notified = false;
  m_renderingEnablingCompletionHandler = [&]()
  {
    lock_guard<mutex> lock(completionMutex);
    notified = true;
    completionCondition.notify_one();
  };

  if (isEnabled)
  {
    // wake up rendering thread
    WakeUp();
  }
  else
  {
    // here we set up value only if rendering disabled
    m_isEnabled = false;

    // if renderer thread is waiting for message let it go
    CancelMessageWaiting();
  }

  unique_lock<mutex> lock(completionMutex);
  completionCondition.wait(lock, [&notified] { return notified; });
}

bool BaseRenderer::FilterGLContextDependentMessage(ref_ptr<Message> msg)
{
  return msg->IsGLContextDependent();
}

void BaseRenderer::CheckRenderingEnabled()
{
  if (!m_isEnabled)
  {
    dp::OGLContext * context = nullptr;

    if (m_wasContextReset)
    {
      EnableMessageFiltering(bind(&BaseRenderer::FilterGLContextDependentMessage, this, _1));
      OnContextDestroy();
    }
    else
    {
      bool const isDrawContext = m_threadName == ThreadsCommutator::RenderThread;
      context = isDrawContext ? m_contextFactory->getDrawContext() :
                                m_contextFactory->getResourcesUploadContext();
      context->setRenderingEnabled(false);
    }

    // notify initiator-thread about rendering disabling
    Notify();

    // wait for signal
    unique_lock<mutex> lock(m_renderingEnablingMutex);
    m_renderingEnablingCondition.wait(lock, [this] { return m_wasNotified; });

    // here rendering is enabled again
    m_wasNotified = false;
    m_isEnabled = true;

    if (m_wasContextReset)
    {
      m_wasContextReset = false;
      DisableMessageFiltering();
      OnContextCreate();
    }
    else
    {
      context->setRenderingEnabled(true);
    }

    // notify initiator-thread about rendering enabling
    // m_renderingEnablingCompletionHandler will be setup before awakening of this thread
    Notify();
  }
}

void BaseRenderer::Notify()
{
  if (m_renderingEnablingCompletionHandler != nullptr)
    m_renderingEnablingCompletionHandler();

  m_renderingEnablingCompletionHandler = nullptr;
}

void BaseRenderer::WakeUp()
{
  lock_guard<mutex> lock(m_renderingEnablingMutex);
  m_wasNotified = true;
  m_renderingEnablingCondition.notify_one();
}

bool BaseRenderer::CanReceiveMessages()
{
  threads::IRoutine * routine = m_selfThread.GetRoutine();
  return routine != nullptr && !routine->IsCancelled();
}

} // namespace df
