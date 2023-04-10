#include "connection.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#if defined(OSX)
extern "C" void CFTemporaryDirectory(char *s, size_t len);
#endif

struct BaseConnectionUnix : public BaseConnection {
    int sock{-1};
};

static BaseConnectionUnix ConnectionUnix;
static sockaddr_un PipeAddr{};
#ifdef MSG_NOSIGNAL
static int MsgFlags = MSG_NOSIGNAL;
#else
static int MsgFlags = 0;
#endif

static const char* GetTempPath(void)
{
#if defined(OSX)
   static char temp[1024];
   CFTemporaryDirectory(temp, 1024);
   return temp;
#else
   const char* temp = getenv("XDG_RUNTIME_DIR");
   temp = temp ? temp : getenv("TMPDIR");
   temp = temp ? temp : getenv("TMP");
   temp = temp ? temp : getenv("TEMP");
   temp = temp ? temp : "/tmp";
   return temp;
#endif
}

/*static*/ BaseConnection* BaseConnection::Create()
{
   PipeAddr.sun_family = AF_UNIX;
   return &ConnectionUnix;
}

/*static*/ void BaseConnection::Destroy(BaseConnection*& c)
{
   auto self = reinterpret_cast<BaseConnectionUnix*>(c);
   self->Close();
   c = nullptr;
}

bool BaseConnection::Open()
{
#ifdef SO_NOSIGPIPE
   int optval           = 1;
#endif
   const char* tempPath = GetTempPath();
   auto self            = reinterpret_cast<BaseConnectionUnix*>(this);

   self->sock = socket(AF_UNIX, SOCK_STREAM, 0);
   if (self->sock == -1)
      return false;
   fcntl(self->sock, F_SETFL, O_NONBLOCK);

#ifdef SO_NOSIGPIPE
   setsockopt(self->sock, SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval));
#endif

   for (int pipeNum = 0; pipeNum < 10; ++pipeNum)
   {
      snprintf(
            PipeAddr.sun_path, sizeof(PipeAddr.sun_path), "%s/discord-ipc-%d", tempPath, pipeNum);
      int err = connect(self->sock, (const sockaddr*)&PipeAddr, sizeof(PipeAddr));
      if (err == 0)
      {
         self->isOpen = true;
         return true;
      }
   }
   self->Close();
   return false;
}

bool BaseConnection::Close()
{
    auto self = reinterpret_cast<BaseConnectionUnix*>(this);
    if (self->sock == -1)
        return false;
    close(self->sock);
    self->sock = -1;
    self->isOpen = false;
    return true;
}

bool BaseConnection::Write(const void* data, size_t length)
{
   ssize_t sentBytes;
   auto self = reinterpret_cast<BaseConnectionUnix*>(this);

   if (self->sock == -1)
      return false;

   sentBytes = send(self->sock, data, length, MsgFlags);
   if (sentBytes < 0)
      Close();
   return sentBytes == (ssize_t)length;
}

bool BaseConnection::Read(void* data, size_t length)
{
   int res;
   auto self = reinterpret_cast<BaseConnectionUnix*>(this);

   if (self->sock == -1)
      return false;

   res = (int)recv(self->sock, data, length, MsgFlags);
   if (res < 0)
   {
      if (errno == EAGAIN)
         return false;
      Close();
   }
   return res == (int)length;
}
