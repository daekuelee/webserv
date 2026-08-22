#include <Pattern/CgiReaderProcessor.hpp>
#include <cstdlib>
#include <cstdio>
#include <cstring>
// To do: implement CgiReaderProcessor
CgiReaderProcessor::CgiReaderProcessor(void) {
}
CgiReaderProcessor::~CgiReaderProcessor(void) {
}


// std::string exec(const char* cmd) {
//     std::string result = "";
//     char buffer[128];
//     FILE* pipe = popen(cmd, "r");
//     if (!pipe) throw std::runtime_error("popen() failed!");
//     try {
//         while (!feof(pipe)) {
//             if (fgets(buffer, 128, pipe) != NULL)
//                 result += buffer;
//         }
//     } catch (...) {
//         pclose(pipe);
//         throw;
//     }
//     pclose(pipe);
//     return result;
// }

// std::map<std::string, std::string> findInterpreters() {
//     std::map<std::string, std::string> interpreters;
//     const char* languages[] = {"perl", "php", "python3"};
//     const size_t numLangs = sizeof(languages) / sizeof(languages[0]);

//     for (size_t i = 0; i < numLangs; ++i) {
//         std::string whichCmd = std::string("which ") + languages[i];
//         std::string path = exec(whichCmd.c_str());
//         if (!path.empty()) {
//             path.erase(path.find_last_not_of(" \n\r\t") + 1); // 개행 및 공백 제거
//             interpreters[languages[i]] = path;
//         }
//     }

//     return interpreters;
// }

// 설치 위치가 플랫폼마다 다르므로 (macOS homebrew vs Linux) 후보를 순서대로
// 탐색한다. 아무것도 없으면 빈 문자열 → 스크립트를 직접 execve (셔뱅에 위임).
static std::string firstExecutable(const char *const *candidates) {
    for (int i = 0; candidates[i]; ++i) {
        if (access(candidates[i], X_OK) == 0)
            return (candidates[i]);
    }
    return "";
}

std::string CgiReaderProcessor::getInterPreterPath(const std::string &extension){
   static std::map<std::string, std::string> interPreterMap;

   if (interPreterMap.empty()){
         static const char *const phpCandidates[] =
             { "/usr/bin/php-cgi", "/opt/homebrew/bin/php-cgi", 0 };
         static const char *const pyCandidates[] =
             { "/usr/bin/python3", "/usr/local/bin/python3", "/opt/homebrew/bin/python3", 0 };
         static const char *const plCandidates[] =
             { "/usr/bin/perl", "/opt/homebrew/bin/perl", 0 };
         interPreterMap["php"] = firstExecutable(phpCandidates);
         interPreterMap["py"] = firstExecutable(pyCandidates);
         interPreterMap["pl"] = firstExecutable(plCandidates);
   }

    if (interPreterMap.find(extension) == interPreterMap.end()){
         return "";
    }
    return interPreterMap[extension];
}
void CgiReaderProcessor::_handleCgiProcess(ft::shared_ptr<CgiChannel> cgiChannel, ft::shared_ptr<Client> client,
    ft::shared_ptr<VirtualServerManager> virtualServerManager, ft::shared_ptr<Channel> channel) {
        try {
            const std::map<std::string, std::string>& env = 
            CgiEnvSetter::getInstance().getEnv(client, channel, virtualServerManager);
            EnvpManager envpManager = CgiEnvSetter::setEnvAll(env);
            cgiChannel->_dupFdInCgiProcess();
            cgiChannel->_closeServerSideFd();
            cgiChannel->_closeCgiSideFd();

            EventQueue &eventQueue = EventQueue::getInstance();
            eventQueue.deleteInstance();

            ft::shared_ptr<HttpRequest> request = client->getRequest();
            std::string fullPath = RouterUtils::findPath(virtualServerManager, request);
            std::string cgiPath =  fullPath.substr(0, fullPath.size() - 
            RouterUtils::findPathInfo(virtualServerManager, request).size());
            size_t dotpos = cgiPath.find_last_of(".");
            std::string extension = cgiPath.substr(dotpos + 1);
            CharArray argv;
            std::string interPreterPath = getInterPreterPath(extension);
            if (cgiPath.empty()) {
                throw std::runtime_error("cgiPath is empty -> logical error");
            }
            if (interPreterPath.empty()) {
                argv = CharArray(1);
                argv.insert(cgiPath);
            }
            else {
                argv = CharArray(2);
                argv.insert(interPreterPath);
                argv.insert(cgiPath);
            }
            execve(argv[0], argv.get(), envpManager.getEnvp());
            perror("execve error");
            if (errno == ENOENT) {
                throw NotFoundException();
            }
            else if (errno == EACCES || errno == EPERM || errno == EISDIR) {
                throw ForbiddenException();
            }
            else {
                throw std::runtime_error("execve error");
            }
        }
        catch (const std::exception &e) {
            // log error CgiChannel execute,e.what
            // throw ;
            // Logger::getInstance().error(e.what());
            FILE *file = fopen(DEFAULT_LOG_FILE_NAME.c_str(), "a+");
            if (file == NULL) {
                std::cerr << "CgiReaderProcessor::_handleCgiProcess::exception::file is null" << std::endl;
                exit(EXIT_FAILURE);
            }
            std::string error = e.what();
            std::string errorWithNewLine = "cgi execute error:" +
            error + "\n";
            fwrite(errorWithNewLine.c_str(), sizeof(char), errorWithNewLine.size(), file);
            fclose(file);
            exit(EXIT_FAILURE);
        }
        exit(EXIT_FAILURE);
}

void CgiReaderProcessor::_onBoardReadFromCgi(int fd, ft::shared_ptr<Client> client) {
    EventFactory& eventFactory = EventFactory::getInstance();

    EventDto eventDto(client, ft::shared_ptr<Channel>(new ByteStream(fd)));
     Event*event = eventFactory.createEvent(ft::READ_EVENT_FROM_CGI, eventDto);
    event->onboardQueue();
}

void CgiReaderProcessor::_onBoardWriteToCgi(int fd, ft::shared_ptr<Client> client) {
    EventFactory& eventFactory = EventFactory::getInstance();
    EventDto eventDto(client, ft::shared_ptr<Channel>(new ByteStream(fd)));
     Event* event = eventFactory.createEvent(ft::WRITE_EVENT_TO_GCI, eventDto);
    event->onboardQueue();
}

void CgiReaderProcessor::_onBoardCgiWait(pid_t pid) {
    EventFactory& eventFactory = EventFactory::getInstance();

    EventDto eventDto(pid);
    Event* event = eventFactory.createEvent(ft::CGI_WAITING_EVENT, eventDto);
    event->onboardQueue();
}

void CgiReaderProcessor::_onBoardGateWayTimeout(pid_t pid) {
    EventFactory& eventFactory = EventFactory::getInstance();

    EventDto eventDto(pid);
    Event* event = eventFactory.createEvent(ft::CGI_KILL_EVENT, eventDto);
    event->onboardQueue();
}
e_pattern_Process_result CgiReaderProcessor::process(ft::shared_ptr
    <VirtualServerManager> virtualServerManager,
    ft::shared_ptr<Client> client,
    ft::shared_ptr<Channel> channel) {
    ft::shared_ptr<CgiChannel> cgiChannel = ft::make_shared<CgiChannel>();
    pid_t pid;
    try {
        cgiChannel->build();
    }
    catch (const std::exception &e) {
        //log error CgiChannel build,e.what
        throw ;
    }
    pid = fork();
    if (pid == -1) {
        throw std::runtime_error("fork error");
    }
    else if (pid == 0) {
        _handleCgiProcess(cgiChannel, client, virtualServerManager, channel);
    }
    else {
        //parent
        cgiChannel->_closeCgiSideFd();
        try {

            _onBoardReadFromCgi(cgiChannel->getPipFd(e_server_read), client);
            if (client->getRequest()->getBodySize() > 0) {
                _onBoardWriteToCgi(cgiChannel->getPipFd(e_server_write), client);
            }
            else {
                cgiChannel->destroy(e_server_write);
            }
            _onBoardCgiWait(pid);
            _onBoardGateWayTimeout(pid);
            // cgiChannel->_setNonBlockServerSideFd();
        }
        
        catch (const std::exception &e) {
            //log error CgiChannel setNonBlock,e.what
            throw ;
        }
        
    }
    return WAITING;
}

e_pattern_Process_result CgiReaderProcessor::querryCanSending(ft::shared_ptr
    <VirtualServerManager> virtualServerManager,
    ft::shared_ptr<Client> client) {
    (void)virtualServerManager;
    ft::shared_ptr<HttpResponse> response = client->getResponse();
    if (response->getCgiSync() == e_cgi_reading_done) {
        return SUCCESS;
    }
    return WAITING;
}