#include "../../../incs/Event/WriteEvent/WriteEventToClientHandler.hpp"
#include "../../../incs/Event/WriteEvent/WriteEventToClient.hpp"
#include <Http/Exception/HttpException.hpp>
#include <Http/Exception/MethodNotAllowedException.hpp>

WriteEventToClientHandler::WriteEventToClientHandler() : WriteEventHandler() {}
WriteEventToClientHandler::~WriteEventToClientHandler() {}



// clientQueue안에서는 동기화 적으로 작동,
// 파일을 보내는걸 완료했는데 클라이언트가 죽은 경우 고려 
void WriteEventToClientHandler::_partialSending(ft::shared_ptr<HttpResponse> response, ft::shared_ptr<Client> client,
WriteEventToClient *curEvent){
	if (response->sendToClient(curEvent->getChannel()) == sendingDone){
		client->clearResponseAndRequest();
		if (client->isClientDie())
			curEvent->offboardQueue();
	}
}

void WriteEventToClientHandler::handleEvent(Event &event){
	WriteEventToClient *curEvent = static_cast<WriteEventToClient *>(&event);
	ft::shared_ptr<Client> client = curEvent->getClient();

	//Todo: check this
	if (client->isRequestEmpty()) {
		if (!client->isResponseEmpty()) {
			if (client->getResponse()->isSending())
				_partialSending(client->getResponse(), client, curEvent);
		}
		else {
			if (client->isClientDie())
				curEvent->offboardQueue();
		}
		//request Empty && !(clientDie || (ResponseExist && isSending) )-> return
		return ;
	}
	if (client->getRequest()->isError()){
		ErrorPageHandler::getErrorPageResponseTo(client, REQUEST_ENTITY_TOO_LARGE);
		client->clientKill();
		_partialSending(client->getResponse(), client, curEvent);	
	}
	try {
		ft::shared_ptr<HttpRequest> curRequest = client->getRequest();
		PatternType patternType = client->getPatternType(curEvent->getVirtualServerManger());
		PatternProcessor &patternProcessor = PatternProcessor::getInstance(patternType,
		curEvent->getVirtualServerManger(), client);
		client->allocateResponse();
		
		// Todo: The processor should pre-populate the normalcase buffer with the appropriate format, 
		// excluding the content body, in anticipation of a successful scenario. 
		if (patternProcessor.process() == WAITING) {
			patternProcessor.clear();
		}
		else if (patternProcessor.process() == SUCCESS) {
			patternProcessor.clear();
			_partialSending(client->getResponse(), client, curEvent);
		}
		else {
			throw std::runtime_error("WriteEventToClientHandler::handleEvent : patternProcessor.process() is FAIL");
		}
	}
	catch (HttpException &e) {
		// Logger::getInstance()->log(LogLevel::ERROR, e.what());
		ErrorPageHandler::getErrorPageResponseTo(client, e.getStatusCode());
		_partialSending(client->getResponse(), client, curEvent);
	}
	catch (std::exception &e) {
		ErrorPageHandler::getErrorPageResponseTo(client, INTERNAL_SERVER_ERROR);
		_partialSending(client->getResponse(), client, curEvent);	
	}
}