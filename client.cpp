#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>
#include <ctype.h>
#include <time.h>
#include <mysql.h>
#include <netdb.h> 

/* -----------------------------------------------------------------------------------------
// Example Data	: VER|01|999ACKN|05|123CHECKIN|aa|bb|cc|dd|ee
// 				: *block[0][...] = VER|01|999
				: *block[1][...] = ACKN|01|123
	:----------------------------------------------------:
							v ---------------- Data in group 
				: *block[0][0] = VER
				: *block[0][1] = 01
				         ^ ------------------- Group of data 
   -----------------------------------------------------------------------------------------
*/

#define	VERSION			"1.1"
const char *SERVER		= "cm.askme.co.th";
const char *PORT		= "555"; 

const char *REFRESH 	= "1";					// Require to server ; second

const char *STATION		= "01";
const char *F_ROOM_CIN	= "4";
const char *F_NAME_CIN	= "6";
const char *F_MV_NROOM	= "11";
const char *F_MV_OROOM	= "12";
const char *F_GUEST_ROOM = "4";
const char *F_GUEST_NAME = "6";
const char *LOG_PATH	= "/var/log/askme/";
const char *CONF_FILE	= "/etc/askme.conf";
const char *DB_SERVER	= "localhost";
const char *DB_USER		= "admin";
const char *DB_PASS		= "dtw@2015";
const char *DB_NAME		= "dtw_db";
const char *TB_NAME		= "radcheck";
const char *FN_CHECKIN	= "1";
const char *FN_ROOMMOVE	= "1";
const char *FN_GUESTCHANGE	= "1";
const char *LOWER_CASE 	= "1";
const char *DEBUG		= "1";
const char *FN_CHANGETHAI = "1";
const char *REP_THAI	= "askme";

int sockfd, newsockfd, portno;									// Sock Manager.
socklen_t clilen;												// Sock Client OBJ.
struct sockaddr_in serv_addr, cli_addr;							// Structure of sock.
struct hostent *server;
int n;
int found_index[512];
time_t now;
struct tm *tm;
const char *wday_name[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *mon_name[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	
const char* block[512][512];			 
int block_len[512];
void error(const char *msg);
int ReceiveString(char *m_rec);										// Return number of block;
int search(const char *m_search, int mx_data);
int fn_checkin_ret(int m_found);
int fn_roommove_ret(int m_found);
int fn_guestchange_ret(int m_found);
int fn_strt(int m_id);
char *UpdateMySQL(char *Query,int m_select);
void trans_log(const char *log_head, const char *log_msg);
void read_config();

int main(int argc, char *argv[])
{
    char buffer[16000];
	int f_send = 0;
	int iden = 1;
	int n_row = 0;
	int found_str = 0;
	char tmp[64];
	read_config();
	printf("\n# Version %s #\n",VERSION);

	while(1)
	{
		f_send = 1;

		portno = atoi(PORT);
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0) 
		{
			error("ERROR opening socket");
			trans_log("->","Error!! opening socket!!");
		}
		server = gethostbyname(SERVER);									// Resolve name to struct hostent
		if (server == NULL) {
			fprintf(stderr,"ERROR, Server not found\n");
			trans_log("->","Server not found!!");
			exit(0);
		}
		bzero((char *) &serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		bcopy((char *)server->h_addr, 													// Set Destination header
			 (char *)&serv_addr.sin_addr.s_addr,										// Set Destination header
			 server->h_length);															// Set Destination header
		serv_addr.sin_port = htons(portno);												// Set Destination port
		if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 		// Connect Server 
		{
			error("ERROR connecting");
			trans_log("->","Error!! connecting socket!!");
		}

		while(f_send)
		{	
			fn_strt(iden);
			bzero(buffer,sizeof(buffer));													// clear buffer
			n = read(sockfd,buffer,sizeof(buffer));											// Check return data -> buffer
			if (n < 0) 
			{
				error("ERROR reading from socket");
				trans_log("->","Error!! Not receive data!!");
			}
			else
			{
				n_row = ReceiveString(buffer);
				
				if(atoi(FN_CHECKIN) == 1)
					fn_checkin_ret(search("CHECKIN",n_row));
				
				if(atoi(FN_ROOMMOVE) == 1)
				{
					strcpy(tmp,"ROOMMOVE");	
					strcat(tmp,STATION);		// Debug block[data]
					fn_roommove_ret(search(tmp,n_row));
				}
				
				if(atoi(FN_GUESTCHANGE) == 1)
				{
					//fn_guestchange_ret(search("GUESTCHANGE",n_row));
				}
				
				trans_log("->",buffer);
				
				iden++;
				
				if(iden>999)iden=1;
				//close(sockfd);
			//	f_send = 0;
				bzero(block,sizeof(block));
				bzero(found_index,sizeof(found_index));
			}
			sleep(atoi(REFRESH));
		}
		close(sockfd);
	}
	
}  

int ReceiveString(char *m_rec)
{
	char *m_tmp = strdup(m_rec);
	char *tok_A ;
	char *tok_B ;
	int stx_count=0;
	int f_start = 0;
	//        v ------------------ Data in group 
	char data[2048][2048];
	//            ^ -------------- Charater in Data
	int row = 0;
	int j = 0;
	int num_block = 0;
		char tmp_log[1024];char tmp[24];
	bzero(data,sizeof(data));
	
	if(strlen(m_tmp)>0)
	{
		while(m_tmp[stx_count]!='\0')
		{
			if(m_tmp[stx_count] == 2)							// STX ascii
			{
				f_start = 1;
				j = 0;
				stx_count++;
				bzero(data[row],sizeof(data[row]));
			}
			else if(f_start != 1)
			{
				stx_count++;
			}
			while(f_start == 1)
			{
				//tolower(m_tmp[stx_count]);						// Convert to lower case
				data[row][j] = m_tmp[stx_count];
				j++;
				stx_count++;
				if(m_tmp[stx_count]==3)
				{
					f_start = 0;
					j = 0;
					row++;
					stx_count++;
				}
			}
		}
	}

	for(int i=0;i<row;i++)
	{
		m_tmp = strdup(data[i]);					// *********** //
		tok_A = m_tmp;
		tok_B = m_tmp;
		num_block = 0;
		while(tok_A != NULL) 
		{
			strsep(&tok_B,",| ");
			memcpy(&block[i][num_block],tok_A,strlen(tok_A));
			//printf("<-<-<-< block[%d][%d] >->->-> %s\n",i,num_block,&block[i][num_block]);
			if(atoi(DEBUG) == 1)
			{
				strcpy(tmp_log,"Block[");	sprintf(tmp,"%d",i);			strcat(tmp_log,tmp); strcat(tmp_log,"]");
				strcat(tmp_log,"[");		sprintf(tmp,"%d",num_block);	strcat(tmp_log,tmp); strcat(tmp_log,"]:");
				strcat(tmp_log,(const char *)&block[i][num_block]);
				trans_log("Debug:",tmp_log);
				bzero(tmp_log,sizeof(tmp_log));bzero(tmp,sizeof(tmp));	
			}
			tok_A = tok_B;
			num_block++;
		} 
		block_len[i] = num_block-1;
		//printf("num_block=%d\n",num_block-1);
		bzero(data[i],sizeof(data[i]));
		free(m_tmp);
	}
	return row;
}

int search(const char *m_search, int mx_data)
{
	int m_group = 0;
	char tmp_block[128];
	int m_chk=0;

	for(int i=0;i<mx_data;i++)
	{
		//printf("Block[%d][%d] = %s\t Max_data = %d\n",i,m_group,&block[i][m_group],mx_data);
		sprintf(tmp_block,"%s",&block[i][m_group]);
		if(strcmp(tmp_block,m_search)==0)
		{
			found_index[m_chk] = i;
			m_chk++;
		}
	} 
	if(m_chk>0)
	{
		return m_chk;
	}
	else 
		return -1;
}

int fn_strt(int m_id)
{
	char buffer[512];
	char tmp[8];
	
	bzero(buffer,sizeof(buffer));
	buffer[0] = 2;
	strcat(buffer,"STRT|");
	strcat(buffer,STATION);
	strcat(buffer,"|");
	sprintf(tmp,"%d",m_id);		strcat(buffer,tmp);		bzero(tmp,sizeof(tmp));
	buffer[strlen(buffer)] = 3;
	n = write(sockfd,buffer,strlen(buffer));
	return n; 
}

int fn_roommove_ret(int m_found)
{
	char tmp_Query[128];
	int tmp_room;
	char room[5];
	char o_pass[64];
		char tmp_log[1024]; char tmp[24];
	if(m_found != -1)
	{
		for(int i=0;i<m_found;i++)
		{
			bzero(room,sizeof(room));
			// Convert Room 
			//printf("Block[%d][%d]=%s\n",found_index[i],0,&block[found_index[i]][0]);
			tmp_room = atoi((const char *)&block[found_index[i]][block_len[found_index[i]]-2]);
			if(tmp_room < 1000)
				sprintf(room,"%d%.2d",(tmp_room/100)*10,(tmp_room%100));
			else
				sprintf(room,"%s",(const char *)&block[found_index[i]][block_len[found_index[i]]-2]);
			
			// Update MySQL

			strcpy(tmp_Query,"SELECT value FROM ");
			strcat(tmp_Query,TB_NAME);
			strcat(tmp_Query," WHERE username LIKE \"%");
			strcat(tmp_Query,room);
			strcat(tmp_Query,"\";");
			strcpy(o_pass,UpdateMySQL(tmp_Query,1));
			
			bzero(room,sizeof(room));
			// Convert Room 
			//printf("Block[%d][%d]=%s\n",found_index[i],0,&block[found_index[i]][0]);
			tmp_room = atoi((const char *)&block[found_index[i]][block_len[found_index[i]]-1]);
			
			if(tmp_room < 1000)
				sprintf(room,"%d%.2d",(tmp_room/100)*10,(tmp_room%100));
			else
				sprintf(room,"%s",(const char *)&block[found_index[i]][block_len[found_index[i]]-1]);
			
			// Update MySQL
			// UPDATE radcheck SET value=LOWER("PASSWORD") WHERE username LIKE "%room";
			strcpy(tmp_Query,"UPDATE ");
			strcat(tmp_Query,TB_NAME);
			strcat(tmp_Query," SET value=");
			if(atoi(LOWER_CASE) == 1)	strcat(tmp_Query,"LOWER(");
			strcat(tmp_Query,"\"");
			strcat(tmp_Query,o_pass);
			strcat(tmp_Query,"\"");
			if(atoi(LOWER_CASE) == 1)	strcat(tmp_Query,")");
			strcat(tmp_Query," WHERE username LIKE \"%");
			strcat(tmp_Query,room);
			strcat(tmp_Query,"\";");
			UpdateMySQL(tmp_Query,0);
			
			
			if(atoi(DEBUG)==3)
			{
				strcpy(tmp_log,"[");	sprintf(tmp,"%d",found_index[i]);	strcat(tmp_log,tmp);	strcat(tmp_log,"]");
				strcat(tmp_log,"O=");	sprintf(tmp,"%d",block_len[found_index[i]]-2);	strcat(tmp_log,tmp);
				strcat(tmp_log,",N=");	sprintf(tmp,"%d",block_len[found_index[i]]-1);	strcat(tmp_log,tmp);
				trans_log("ROOMMOVE",tmp_log);
				bzero(tmp_log,sizeof(tmp_log));
			}
		}
		
	}
}
int fn_guestchange_ret(int m_found)
{
	char tmp_Query[128];
	int tmp_room;
	char room[5];

	if(m_found != -1)
	{
		for(int i=0;i<m_found;i++)
		{
			bzero(room,sizeof(room));
			// Convert Room 
			//printf("Block[%d][%d]=%s\n",found_index[i],0,&block[found_index[i]][0]);
			tmp_room = atoi((const char *)&block[found_index[i]][atoi(F_GUEST_ROOM)]);
			
			if(tmp_room < 1000)
				sprintf(room,"%d%.2d",(tmp_room/100)*10,(tmp_room%100));
			else
				sprintf(room,"%s",(const char *)&block[found_index[i]][atoi(F_GUEST_ROOM)]);
			
			// Update MySQL
			strcpy(tmp_Query,"UPDATE ");
			strcat(tmp_Query,TB_NAME);
			strcat(tmp_Query," SET value=");
			if(atoi(LOWER_CASE) == 1)	strcat(tmp_Query,"LOWER(");
			strcat(tmp_Query,"\"");
			strcat(tmp_Query,(const char *)&block[found_index[i]][atoi(F_GUEST_NAME)]);
			strcat(tmp_Query,"\"");
			if(atoi(LOWER_CASE) == 1)	strcat(tmp_Query,")");
			strcat(tmp_Query," WHERE username LIKE \"");
			strcat(tmp_Query,room);
			strcat(tmp_Query,"\";");
			UpdateMySQL(tmp_Query,0);
			
			if(atoi(DEBUG)==4)
			{
				//strcpy(tmp_log,"[");	sprintf(tmp,"%d",found_index[i]);	strcat(tmp_log,tmp);	strcat(tmp_log,"]");
				//strcat(tmp_log,"O=");	sprintf(tmp,"%d",block_len[found_index[i]]-2);	strcat(tmp_log,tmp);
				//strcat(tmp_log,",N=");	sprintf(tmp,"%d",block_len[found_index[i]]-1);	strcat(tmp_log,tmp);
				//trans_log("ROOMMOVE",tmp_log);
				//bzero(tmp_log,sizeof(tmp_log));
			}
		}
	}
}
int fn_checkin_ret(int m_found)
{
	char tmp_Query[128];
	int tmp_room;
	char room[5];
	int tmp=0;
	const char * chk_tmp="?";
	if(m_found != -1)
	{
		for(int i=0;i<m_found;i++)
		{

			bzero(room,sizeof(room));
			// Convert Room 
			//printf("Block[%d][%d]=%s\n",found_index[i],0,&block[found_index[i]][0]);
			tmp_room = atoi((const char *)&block[found_index[i]][atoi(F_ROOM_CIN)]);
			
			if(tmp_room < 1000)
				sprintf(room,"%d%.2d",(tmp_room/100)*10,(tmp_room%100));
			else
				sprintf(room,"%s",(const char *)&block[found_index[i]][atoi(F_ROOM_CIN)]);
			
			// Update MySQL
			
			strcpy(tmp_Query,"UPDATE ");
			strcat(tmp_Query,TB_NAME);
			strcat(tmp_Query," SET value=");
			if(atoi(LOWER_CASE) == 1)	strcat(tmp_Query,"LOWER(");
			strcat(tmp_Query,"\"");
			
// Start EDIT
			if(atoi(FN_CHANGETHAI) == 1)
			{
				tmp = strcmp((const char *)&block[found_index[i]][atoi(F_NAME_CIN)],chk_tmp);
				if((tmp <= 0) || (tmp == 63))
					strcat(tmp_Query,REP_THAI);
				else
					strcat(tmp_Query,(const char *)&block[found_index[i]][atoi(F_NAME_CIN)]);		
			}
			else
			{
				strcat(tmp_Query,(const char *)&block[found_index[i]][atoi(F_NAME_CIN)]);		
			}
// END EDIT			
			
			strcat(tmp_Query,"\"");
			if(atoi(LOWER_CASE) == 1)	strcat(tmp_Query,")");
			strcat(tmp_Query," WHERE username LIKE \"");
			strcat(tmp_Query,room);
			strcat(tmp_Query,"\";");
			//printf("Query Command = %s\n",tmp_Query);
			UpdateMySQL(tmp_Query,0);
			
			if(atoi(DEBUG)==2)
			{
				//strcpy(tmp_log,"[");	sprintf(tmp,"%d",found_index[i]);	strcat(tmp_log,tmp);	strcat(tmp_log,"]");
				//trans_log("CHECKIN",tmp_log);
				//bzero(tmp_log,sizeof(tmp_log));
			}
		}
	}
}
void read_config()
{
	char rd_str[256];
	char *config[64][2];
	int line = 0;
	int member = 0;
	char *tmp;
	char tmp_chk[15];
	FILE* cfg_ptr = fopen(CONF_FILE,"r");
	bzero(&config,sizeof(config));
	while(fgets(rd_str,sizeof(rd_str),cfg_ptr))
	{
		//bzero(&tmp,sizeof(tmp));
		
		if(rd_str[0] != '#')
		{
			tmp = strdup(rd_str);
			tmp = strtok(tmp,"= \n\t");			
			while(tmp != NULL)
			{
				config[line][member] = tmp;
				//printf("%s\n",config[line][member]);
				tmp = strtok(NULL,"= \n\t");
				if(member == 0)
					member++;
				else
				{
					member = 0;
					line++;
				}
			}
		}
	}
	fclose(cfg_ptr);
	bzero(tmp_chk,sizeof(tmp_chk));
	//printf("%s\n",config[0][0]);
	for(int i=0;i<line;i++)
	{
		//printf("%s:%s\n",config[i][0],config[i][1]);
		sprintf(tmp_chk,"%s",config[i][0]);
		if(strcmp(tmp_chk,"STATION") == 0)
			STATION = config[i][1];
		if(strcmp(tmp_chk,"SERVER") == 0)
			SERVER = config[i][1];
		if(strcmp(tmp_chk,"PORT") == 0)
			PORT = config[i][1];
		if(strcmp(tmp_chk,"REFRESH") == 0)
			REFRESH = config[i][1];
		if(strcmp(tmp_chk,"FIELD_NAME") == 0)
			F_NAME_CIN = config[i][1];
		if(strcmp(tmp_chk,"FIELD_ROOM") == 0)
			F_ROOM_CIN = config[i][1];
		if(strcmp(tmp_chk,"LOG_PATH") == 0)
			LOG_PATH = config[i][1];
		if(strcmp(tmp_chk,"DB_SERVER") == 0)
			DB_SERVER = config[i][1];
		if(strcmp(tmp_chk,"DB_USER") == 0)
			DB_USER = config[i][1];
		if(strcmp(tmp_chk,"DB_PASS") == 0)
			DB_PASS = config[i][1];
		if(strcmp(tmp_chk,"DB_NAME") == 0)
			DB_NAME = config[i][1];
		if(strcmp(tmp_chk,"TB_NAME") == 0)
			TB_NAME = config[i][1];
		if(strcmp(tmp_chk,"F_MOVE_OLD") == 0)
			F_MV_OROOM = config[i][1];
		if(strcmp(tmp_chk,"F_MOVE_NEW") == 0)
			F_MV_NROOM = config[i][1];
		if(strcmp(tmp_chk,"F_GUEST_ROOM") == 0)
			F_GUEST_ROOM = config[i][1];
		if(strcmp(tmp_chk,"F_GUEST_NAME") == 0)
			F_GUEST_NAME = config[i][1];
		if(strcmp(tmp_chk,"FN_CHECKIN") == 0)
			FN_CHECKIN = config[i][1];
		if(strcmp(tmp_chk,"FN_ROOMMOVE") == 0)
			FN_ROOMMOVE = config[i][1];
		if(strcmp(tmp_chk,"FN_GUESTCHANGE") == 0)
			FN_GUESTCHANGE = config[i][1];
		if(strcmp(tmp_chk,"LOWER_CASE") == 0)
			LOWER_CASE = config[i][1];
		if(strcmp(tmp_chk,"DEBUG") == 0)
			DEBUG = config[i][1];
		if(strcmp(tmp_chk,"FN_CHANGETHAI") == 0)
			FN_CHANGETHAI = config[i][1];
		if(strcmp(tmp_chk,"REP_THAI") == 0)
			REP_THAI = config[i][1];
	}

}
void trans_log(const char *log_head, const char *log_msg)
{
	char result[26];
	char log_tmp[64];
	
	now = time(0);
	if((tm = localtime(&now)) == NULL)
			printf("Error extracting time\n");
		
	sprintf(log_tmp,"%s%d-%.3s-%.2d.log",LOG_PATH,1900+tm->tm_year,mon_name[tm->tm_mon],tm->tm_mday);
	
	FILE *fptr;
	fptr = fopen(log_tmp,"ab");
	
	if(fptr == NULL)
	{ 
		printf("Create Log file Error !");
		exit(1);
	}

	sprintf(result, "%.3s %.3s%3d %.2d:%.2d:%.2d %d",
					wday_name[tm->tm_wday],
					mon_name[tm->tm_mon],
					tm->tm_mday, tm->tm_hour,
					tm->tm_min, tm->tm_sec,
					1900 + tm->tm_year);
					
	fprintf(fptr,"%s %s %s\n",result,log_head,log_msg);
	fclose(fptr);
	bzero(result,sizeof(result));
	bzero(log_tmp,sizeof(log_tmp));
}
char *UpdateMySQL(char *Query,int m_select)
{ 
	MYSQL *conn;
	MYSQL_RES *res;
	MYSQL_ROW row;
	static char tmp[64];
	//printf("%s\n",Query);
	
	conn = mysql_init(NULL);
	// Connect DB //
	if(!mysql_real_connect(conn,DB_SERVER,DB_USER,DB_PASS,DB_NAME,0,NULL,0))
	{
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}
	if(mysql_query(conn,Query))
	{
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}
	
	res = mysql_use_result(conn);
	strcpy(tmp,"OK");
	if(m_select == 1)
	{
		if(res == NULL)
		{
			fprintf(stderr, "%s\n", mysql_error(conn));
			exit(1);
		}
		else
		{
			while((row = mysql_fetch_row(res)))
			{
				strcpy(tmp,row[0]);
			}
		}
	}
	mysql_close(conn);
	return tmp;
}
void error(const char *msg)
{
    perror(msg);
    exit(1);
}