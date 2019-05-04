#include "WTFheader.h"

struct mutexNode* mutexList = NULL;
struct threadNode* threadList = NULL;

void exitSignalHandler( int sig_num ){
	while( threadList != NULL ){
		close(threadList->sockfd);
		if( threadList->next == NULL ){
			printf("name: %s\n", threadList->name);
			printf("Server: Server has been shut down.\n");
		}else{
			//TODO: should probably identify the client
			printf("Server: A client has been disconnected\n");
		}
		pthread_cancel(threadList->thread);
		threadList = threadList->next;
	}

	exit(0);
}

void *threadHandler(void *fd_pointer){
	int sockfd = *(int*)fd_pointer;
	struct node* dataList = receiveData(sockfd);

	executeCommand(dataList, sockfd);

	threadList = threadList->next; //remove finished thread from list

	//Command has been executed here
	close(sockfd); //close client connection
	printf("Server: Client disconnected\n");
}


int main( int argc, char** argv ){

	signal(SIGINT, exitSignalHandler);

	int port = -1;
	//read in argument <port>
	if( argc != 2 ){
		printf("Error: Invalid arguments\n");
		printf("Usage: ./WTFserver <port>\n");
		return 1;
	}else{
		port = atoi(argv[1]); //string to integer
		//printf("port: %s, %d\n", argv[1], port);
	}

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		printf("Error: Can't open socket\n"); return 1;
	}

	//adds server listening thread to threadList
	struct threadNode* serverThread = (struct threadNode*) malloc(sizeof(struct threadNode));
	serverThread->sockfd = sockfd;
	serverThread->name = "server";
	threadList = serverThread;


	struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = INADDR_ANY;
	serverAddress.sin_port = htons(port);

	if( bind(sockfd,(struct sockaddr*) &serverAddress, sizeof(serverAddress)) < 0 ){
		printf("Error: can't bind.\n"); return 1;
	}

	printf("Server: running...\n");

	//Sets up mutex for each project in server
	DIR* dir = opendir(".");
	struct dirent* entry;
	while((entry=readdir(dir)) != NULL ){
		if(entry->d_type == DT_DIR
			&& strcmp(entry->d_name, ".")!=0 && strcmp(entry->d_name, "..")!=0){
			//Do stuff
			struct mutexNode* addThis
						 = (struct mutexNode*) malloc(sizeof(struct mutexNode));
			addThis->projectname = entry->d_name;
			addThis->mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
			addThis->next = mutexList;
			mutexList = addThis;
		}
	}
	closedir(dir);


	listen(sockfd, 20);
	struct sockaddr_in clientAddress;
	socklen_t clientLen = sizeof(clientAddress);

	//loop listens for connections
	int connectionfd;
	while(1){
		connectionfd = accept(sockfd, (struct sockaddr *) &clientAddress, &clientLen);
		if(connectionfd<0){
			printf("Error: Client connection failed.\n"); continue;
		}
		printf("Server: New client connected.\n");

		//new thread for new client
		int* newsockfdptr;
		newsockfdptr = malloc(sizeof(int));
		*newsockfdptr = connectionfd;

		struct threadNode* currentThread =
					(struct threadNode*)malloc(sizeof(struct threadNode));
		currentThread->sockfd = connectionfd;
		currentThread->next = threadList;
		currentThread->name = "client";
		threadList = currentThread;

		pthread_create(&(currentThread->thread), NULL, threadHandler, (void*) newsockfdptr);

	}

	return 0;
}

void executeCommand(struct node* dataList, int sockfd){

	struct mutexNode* currentMutex = NULL; //FIND MUTEX FOR THE PROJECT

	char* command = dataList->name;

	if(strcmp(command, "checkout")==0){
    		serverCheckout(dataList->PROJECTNAME, sockfd);
	}else if(strcmp(command, "update")==0){
		serverSendManifest(dataList, sockfd);
	}else if(strcmp(command, "upgrade")==0){
		serverUpgrade(dataList, sockfd);
	}else if(strcmp(command, "commit")==0){
		serverCommit(dataList, sockfd);
	}else if(strcmp(command, "push")==0){
		serverPush(dataList, sockfd);
	}else if(strcmp(command, "create")==0){
		serverCreate(dataList, sockfd);
	}else if(strcmp(command, "destroy")==0){
		serverDestroy(dataList->PROJECTNAME, sockfd);
	}else if(strcmp(command, "currentversion")==0){
		serverSendManifest(dataList, sockfd);
	}else if(strcmp(command, "history")==0){
		serverHistory(dataList, sockfd);
	}else if(strcmp(command, "rollback")==0){
		serverRollback(dataList,sockfd);
	}else{
		//should never happen
	}
}

void serverRollback(struct node * dataList, int sockfd) {
	char * projectname = dataList->PROJECTNAME;
	struct node * ptr = dataList;
	char * versionAndProject = NULL;

	//find version and project info
	while (ptr!=NULL) {
		if (strcmp(ptr->nodeType,"project") == 0) {
			versionAndProject = ptr->name;
		}
		ptr=ptr->next;
	}

	//break version and project into 2 strings
	char * pname = NULL; //project name
	char * version = NULL; //desired version to roll back to
	char * token = NULL;
	int i = 0;
	while(versionAndProject[i]!= '\t'){
		token = appendChar(token,versionAndProject[i]);
		i++;
	}
	pname = token; //project
	token = NULL;
	i++; //skip delim
	while(i < strlen(versionAndProject)){
		token = appendChar(token, versionAndProject[i]);
		i++;
	}
	version = token; //version
	printf("p: %s\nv: %s\n",pname,version);


}

void serverHistory(struct node * dataList, int sockfd) {
	char * projectname = dataList->PROJECTNAME;
	char * projectpath = getPath(".", projectname);
	char * historyPath = getPath(projectpath, HISTORY);

	if( dirExists(projectpath) == 0 ){
		sendData(sockfd, makeMsg(dataList->name, "Error", "Project not on server"));
		return; //unsuccessful
	}

	char* data = versionData(dataList->name,projectname, historyPath);

	sendData(sockfd, data); //Sends data to client

}

void serverDestroy(char* projectname, int sockfd) {

	pthread_mutex_t mutex = getMutex(projectname, mutexList)->mutex;
	pthread_mutex_lock(&mutex); //locks project

	char* projectPath = getPath(".", projectname);
	if( dirExists(projectPath) == 0 ){
		sendData(sockfd, makeMsg("destroy", "Error", "Project not on server"));
		pthread_mutex_unlock(&mutex); //unlocks
		return;
	}

	destroyRecursive(projectPath);

	//remove mutex for the project
	struct mutexNode* ptr = mutexList;
	struct mutexNode* prev = NULL;
	while( ptr != NULL && strcmp(ptr->projectname, projectname)!=0 ){
		prev = ptr;
		ptr = ptr->next;
	}
	if( prev == NULL ){ //project is first node
		mutexList = mutexList->next;
	}else{
		prev->next = ptr->next;
	}

	sendData(sockfd, makeMsg("destroy", "success", "Project destroyed"));

	pthread_mutex_unlock(&mutex); //unlocks

}

void serverCheckout(char* projectname, int sockfd) {
	char* projectPath = getPath(".", projectname);

	//Fails if project not on server
	if( dirExists(projectPath) == 0 ){
		sendData(sockfd, makeMsg("checkout", "Error", "Project not on server"));
		return;
	}

	//Read in server's manifest data
	char * manPath = getPath(projectPath,MANIFEST);
	char * manData = readFileData(manPath);
	struct manifestNode * manList = parseManifest(manData);


	//make data to send to client
	struct manifestNode* ptr = manList->next;
	char * fileList = "";
	char * contentList = "";
	char* data = NULL;
	int count = 1;
	data = appendFileData(data, manPath); //adds manifest to data
	while( ptr != NULL ){
		data = appendFileData(data, ptr->path); //adds files to data
		ptr = ptr->next;
		count++;
	}
	data = appendData(dataHeader("checkout", "ProjectFileContent", projectname, count), data);

	sendData(sockfd, data); //Sends data to client
}

void serverCommit(struct node* dataList, int sockfd){

	char* projectPath = getPath(".", dataList->PROJECTNAME);

	//sends manifest, fails if project or manifest not found
	if(serverSendManifest(dataList, sockfd)==0){
		return;
	}

	dataList = receiveData(sockfd); //gets the new commit data
	//count num of active commits in .commit folder
	char* commitFolderPath = getPath(projectPath, COMMIT);
	int countFiles = 1;
	DIR* dir = opendir(commitFolderPath);
	struct dirent* entry;
	while((entry=readdir(dir)) != NULL ){
		if(entry->d_type == DT_REG){
			countFiles++;
		}
	}
	closedir(dir);

	//create a new commit in .commit folder
	int commitFD = createFile(getPath(commitFolderPath, int2str(countFiles)));
	char* writeout = dataList->FIRSTFILENODE->content;
	write(commitFD, writeout, strlen(writeout));
	close(commitFD);
}

void writeHistory(struct manifestNode * newCommits, int newVersion, char * projectpath) {
	printf("server 304\n");
	char * historyPath = getPath(projectpath, HISTORY);

	int historyFD = open(historyPath, O_WRONLY|O_APPEND);
	char* versionNum = int2str(newVersion);
	write(historyFD, "\n", 1);
	write(historyFD, versionNum, strlen(versionNum));
	close(historyFD);
	printf("server311\n");
	
	struct manifestNode * ptr = newCommits->next;
	while (ptr!=NULL) {
		writeToVersionFile(historyPath, ptr->code, ptr->version, ptr->path, ptr->hash);
		ptr = ptr->next;
	}
}

void serverPush(struct node* dataList, int sockfd){

	char* projectPath = getPath(".", dataList->PROJECTNAME);

	pthread_mutex_t mutex = getMutex(dataList->PROJECTNAME, mutexList)->mutex;
	pthread_mutex_lock(&mutex); //locks project

	//Fails if project not on server
	if( dirExists(projectPath) == 0 ){
		sendData(sockfd, makeMsg("push", "Error", "Project not on server"));
		pthread_mutex_unlock(&mutex); //unlocks
		return;
	}

	char* commitFPath = getPath(projectPath, COMMIT);

	//tries to find matching commit
	int commitFound = 0;
	DIR* dir = opendir(commitFPath);
	struct dirent* entry;
	while((entry=readdir(dir)) != NULL ){
		if(entry->d_type == DT_REG){
			if( strcmp(dataList->FIRSTFILENODE->content,
					readFileData(getPath(commitFPath, entry->d_name))) == 0 ){
				commitFound = 1;
			}
		}
	}
	closedir(dir);
	if( commitFound == 0 ){ //if matching commit not found
		sendData(sockfd, makeMsg("push", "Error", "Commit before pushing"));
		return;
	}
	//remove all commits
	dir = opendir(commitFPath);
	while((entry=readdir(dir)) != NULL ){
		if(entry->d_type == DT_REG){
			remove(getPath(commitFPath, entry->d_name));
		}
	}
	closedir(dir);

	//read in manifest
	char* manPath = getPath(projectPath, MANIFEST);
	struct manifestNode* mList = parseManifest(readFileData(manPath));
	int versionNum = mList->version;


	/*
		TODO: More EC: do this with zlib (zlib cant compress directories)
		figure out a format to separate files
		zlib compresses into a single file
		undo format to reconstruct files from the single compressed file
	*/

	printf("server374\n");
	char* tarPath = getPath(projectPath, 
				getPath(ARCHIVE, append(int2str(versionNum), ".tar.gz")));
	printf("tarpath: %s\n", tarPath);
	
	//copy project to temporary ./.projectname on server
	char* tempPath = getPath(".", append(".", dataList->PROJECTNAME));
	createDir(tempPath);
	copydir(projectPath, tempPath);

	//make tar of file in .version
	char* syscmd = append("tar -czvf ", tarPath);
	syscmd = append(syscmd, " ");
	syscmd = append(syscmd, append(".", dataList->PROJECTNAME));	
	system(syscmd);
	
	//delete temporary project copy
	destroyRecursive(tempPath);

	printf("server391\n");
	
	//remove all deleted commits from list of commits
	struct manifestNode* cList = parseManifest(dataList->FIRSTFILENODE->content);
	struct manifestNode* cPtr = cList->next;
	while( cPtr != NULL ){
		//remove all deleted commits from list of commits
		if(strcmp(cPtr->code, "deleted")==0){
			struct manifestNode* mNode = findFile(cPtr->path ,mList);
			mNode->code = "deleted";
			remove(cPtr->path);
		}
		cPtr = cPtr->next;
	}
	printf("server405\n");
	//create/rewrite all the files sent
	struct node* ptr = dataList->FIRSTFILENODE->next;
	while( ptr != NULL ){

		struct manifestNode* cNode = findFile(ptr->name, cList);
		struct manifestNode* mNode = findFile(ptr->name, mList);

		if( mNode == NULL ){
			struct manifestNode* addThis = (struct manifestNode*)malloc(1*sizeof(struct manifestNode));
			addThis->code = "uptodate";
			addThis->version = cNode->version;
			addThis->path = cNode->path;
			addThis->hash = cNode->hash;
			addThis->next = mList->next;
			mList->next = addThis;
		}else if(strcmp(mNode->code, "deleted")==0){
			ptr=ptr->next;
			continue;
		}else{
			mNode->code = "uptodate";
			mNode->version = cNode->version;
			mNode->hash = cNode->hash;
		}

		int fd = open( ptr->name, O_WRONLY|O_CREAT|O_TRUNC, 0666 );
		if( fd<0 ){ //can't open, have to create dirs then retry
			char* tempPath = (char*)malloc((strlen(ptr->name)+1)*sizeof(char));
			strcpy(tempPath, ptr->name);
			createSubdir(tempPath);
			fd = open( ptr->name, O_WRONLY|O_CREAT|O_TRUNC, 0666 );
			if( fd<0){ //should never happen
				sendData(sockfd,makeMsg("push", "Error", "Push failed"));
				return;
			}
		}

		if( ptr->content != NULL ){
			write(fd, ptr->content, strlen(ptr->content));
		}
		close(fd);
		ptr=ptr->next;
	}

	printf("server449\n");	
	
	int newVersion = (mList->version)+1;
	newVersionFile( newVersion , manPath);
	mList = mList->next;
	while( mList != NULL ){
		if( strcmp(mList->code, "deleted") != 0 ){
			writeToVersionFile(manPath, mList->code, mList->version, mList->path, mList->hash);
		}
		mList = mList->next;
	}
	printf("server459\n");
	
	

	writeHistory(cList, newVersion ,projectPath);

	sendData(sockfd, versionData("push", dataList->PROJECTNAME, manPath));
	pthread_mutex_unlock(&mutex); //unlocks

}

//sends the manifest for project to client
//checks if project exists
//checks if file exists
int serverSendManifest(struct node* dataList, int sockfd){
	char* projectname = dataList->PROJECTNAME;
	char* projectPath = getPath(".", projectname);

	//Checks if project exists
	if( dirExists(projectPath) == 0 ){
		sendData(sockfd, makeMsg(dataList->name, "Error", "Project not on server"));
		return 0; //unsuccessful
	}

	//Checks if manifest exists
	char* manPath = getPath(projectPath, MANIFEST);
	if( fileExists(manPath) == 0 ){
		sendData(sockfd, makeMsg(dataList->name, "Error", "Manifest not found on server"));
		return 0;
	}

	//sends manifest
	char* data = versionData(dataList->name,projectname, manPath);
	sendData(sockfd, data);
	return 1;
}


//Create project and manifests, sends manifest to client
void serverCreate(struct node* dataList, int sockfd){

	char* projectname = dataList->PROJECTNAME;
	char* projectpath = getPath(".", projectname);

	//fails if project already exists
	if( dirExists(projectpath)== 1 ){
		sendData(sockfd, makeMsg("create", "Error", "Project already exists on server"));
		return;
	}

	//adds mutex for the project dir
	struct mutexNode* addThis
				 = (struct mutexNode*) malloc(sizeof(struct mutexNode));
	addThis->projectname = projectname;
	addThis->next = mutexList;
	mutexList = addThis;


	//initializes project on server
	createDir(projectpath);
	createDir(getPath(projectpath, ARCHIVE));
	createDir(getPath(projectpath, COMMIT));
	char* manifestPath = getPath(projectpath, MANIFEST);
	newVersionFile(1, manifestPath);

	//create .history file
	newVersionFile(1, getPath(projectpath, HISTORY));

	//Sends initialized manifest to client
	sendData(sockfd, versionData("create",projectname, manifestPath));
}


void serverUpgrade(struct node* dataList, int sockfd){
	//fails if project doesnt exist
	if( dirExists(getPath(".", dataList->PROJECTNAME)) == 0 ){
		sendData(sockfd, makeMsg("upgrade", "Error", "Project not on server"));
		return;
	}

	//read in .update sent from server
	struct manifestNode* uList = parseManifest(dataList->FIRSTFILENODE->content);
	char* data = NULL; int count = 0;
	uList = uList->next;

	//makes data to be sent of all files to be added/updated
	while( uList != NULL ){
		//printf("path: %s\n", uList->path);
		if( uList->code != "D" ){
			data = appendFileData(data, uList->path);
			count++;
		}
		uList = uList->next;
	}
	//Sends the data to client
	data = appendData(dataHeader("upgrade", "ProjectFileContent", dataList->PROJECTNAME, count), data);
	sendData(sockfd, data);
}
