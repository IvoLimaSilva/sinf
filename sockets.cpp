#include <iostream>		// cout
#include <sstream>		// cout
#include <stdlib.h>		// exit
#include <string.h>		// bzero
#include <unistd.h>		// close

#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include <pthread.h>

#include <time.h>
#include <postgresql/libpq-fe.h>

#include <set>

using namespace std;

PGconn* conn = NULL;

set <int> clients;

struct curr_user
{
	int id;
	string name;
	bool admin;
} curr_user;

void empty_curr_user ()
{
	curr_user.name = "";
	curr_user.id = 0;
	curr_user.admin = false;
}

/* FUNÇOES DE ESCRITA ENTRE SOCKETS */

/* Envia uma string para um socket */
void writeline (int socketfd, string line)
{
	string tosend = line + "\n";
	write(socketfd, tosend.c_str(), tosend.length());
}

/* Lê uma linha de um socket; Retorna false se o socket se tiver fechado */
bool readline (int socketfd, string &line)
{
	int
		n; 

	char
		buffer[1025]; //buffer de tamanho 1025 para ter espaço para o \0 que indica o fim de string

	/* inicializar a string */
	line = "";

	// Enquanto não encontrarmos o fim de linha vamos lendo mais dados da stream
	while (line.find ('\n') == string::npos)
	{
		// leu n carateres. se for zero chegamos ao fim
		int n = read (socketfd, buffer, 1024); // ler do socket

		if (n == 0)
			return false; // nada para ser lido -> socket fechado

		buffer[n] = 0; // colocar o \0 no fim do buffer
		line += buffer; // acrescentar os dados lidos à string
	}

	// Retirar o \r\n (lemos uma linha mas não precisamos do \r\n)
	line.erase (line.end () - 1);
	line.erase (line.end () - 1);
	return true;  
}

/* Envia uma mensagem para todos os clientes ligados exceto 1 */
void broadcast (int origin, string text)
{
	/* Usamos um ostringstream para construir uma string
	Funciona como um cout mas em vez de imprimir no ecrã
	imprime numa string */
	ostringstream
		message;

	message << origin << " said: " << text;

	// Iterador para sets de inteiros 
	set<int>::iterator it;
	
	for (it = clients.begin (); it != clients.end (); it++)
	{
		if (*it != origin)
			writeline (*it, message.str ());
	}
}

/* FUNÇOES DE ESCRITA ENTRE SOCKETS */

/* FUNÇOES DE CONTACTO COM A BASE-DE-DADOS */

PGresult* executeSQL(string sql)
{
	PGresult* res = PQexec(conn, sql.c_str());

	if (!(PQresultStatus (res) == PGRES_COMMAND_OK || PQresultStatus (res) == PGRES_TUPLES_OK))
	{
		cout << "Não foi possível executar o comando: >" << sql << "<" << endl << ">" << sql.c_str() << "<" << endl << " = " << PQresultStatus (res) << endl;
		return NULL;
	}

	return res;
}

void initDB()
{
	conn = PQconnectdb ("host='vdbm.fe.up.pt' user='sinf15g34' password='eu' dbname='sinf15g34'");

	if (!conn)
	{
		cout << "Não foi possivel ligar a base de dados 1" << endl;
		exit(-1);
	}

	if (PQstatus(conn) != CONNECTION_OK)
	{
		cout << "Não foi possivel ligar a base de dados 2" << endl;
		exit(-1);
	}
	else
	{
		executeSQL ("SET SCHEMA 'projeto'");
	}
}

void closeDB ()
{
	PQfinish (conn);
}

/* FUNÇOES DE CONTACTO COM A BASE-DE-DADOS */

void print_curr_user ()
{
	cout << "ID: " << curr_user.id << endl
	<< "Name: " << curr_user.name << endl
	<< "Admin: " << curr_user.admin << endl;
}

/* FUNÇOES DO JOGO */

void cmd_help (int socketfd)
{
	ostringstream
		oss;
	string
		data;

	oss << "Pode usar umas das seguintes funções:" << endl
	<< "-------------------------------------" << endl
	<< "\\help ..............................." << endl
	<< "Lista os comandos todos disponíveis ;" << endl << endl
	<< "\\register <name> <password> ........." << endl
	<< "Regista um user com name e password ;" << endl << endl
	<< "\\login <name> <password> ........." << endl
	<< "Login do user com a sua password    ;" << endl << endl
	<< "\\logout .........................." << endl
	<< "Faz logout da sessão actual         ;" << endl << endl
	<< "\\listusers .........................." << endl
	<< "Lista todos os users registados     ;" << endl << endl;

	data = oss.str();
	writeline (socketfd, data);
}

void cmd_register (int socketfd, string &line)
{
	string
		comando,
		user,
		pass;
	istringstream
		iss(line);
 
	iss >> comando >> user >> pass;
	
	if (pass.size() < 4)
	{
		writeline (socketfd, "Palavra-passe muito pequena!");
		return ;
	}
	else
	{	
		PGresult* res = executeSQL ("SELECT * FROM players WHERE name = '" + user + "'");
		
		if (PQntuples (res) > 0) // caso já existe um utilizador com o mesmo username 
		{
			writeline (socketfd, "Já existe um utilizador com este nome!");
		}
		else // caso não exista este username
		{
			executeSQL ("INSERT INTO players (name, password, admin, online) VALUES ('" + user + "', '" + pass + "', FALSE, FALSE)"); //não é admin nem está online
		}
	}
}

void cmd_login (int socketfd, string &line)
{
	string
		comando,
		user,
		pass;
	istringstream
		iss(line);
 
	iss >> comando >> user >> pass;
	
	if (curr_user.id != 0)
	{
		writeline (socketfd, "Já existe uma sessão iniciada. Faça \\logout");
		return ;
	}

	PGresult* res = executeSQL ("SELECT * FROM players WHERE name = '" + user + "' AND password = '" + pass + "' AND online = FALSE");
	
	if (PQntuples (res) == 1) // sucesso 
	{
		curr_user.id = atoi (PQgetvalue (res, 0, 0));
		curr_user.name = PQgetvalue (res, 0, 1);
		if (strcmp (PQgetvalue (res, 0, 3), "TRUE") == 0)
			curr_user.admin = true;
		else
			curr_user.admin = false;

		print_curr_user ();
		
		ostringstream
			oss;

		oss << "UPDATE players SET online = TRUE WHERE id = " << curr_user.id;

		executeSQL (oss.str());
		writeline (socketfd, "Login com sucesso!");
	}
	else // insucesso
	{
		writeline (socketfd, "Erro: username/password errados ou conta já está aberta noutro client.");
	}
}

void cmd_logout (int socketfd)
{
	ostringstream
		oss;

	oss << "UPDATE players SET online = FALSE WHERE id = " << curr_user.id;

	executeSQL (oss.str());
	writeline (socketfd, "Logout com sucesso!");
	
	empty_curr_user ();
}

void cmd_question (int socketfd, string &line)
{
	string
		comando,
		question,
		answer,
		wrong1,
		wrong2,
		wrong3,
		data;
	istringstream
		iss (line);
	ostringstream
		oss;
 
	iss >> comando >> question >> answer >> wrong1 >> wrong2 >> wrong3;
	
	PGresult* res = executeSQL ("SELECT * FROM questions WHERE question = '" + question + "'");
		
	if (PQntuples (res) > 0) // caso já existe esta questao 
	{
		writeline (socketfd, "Já existe esta pergunta!");
	}
	else // caso não exista esta questao
	{
		executeSQL ("INSERT INTO questions (question, answer, wrong1, wrong2, wrong3) VALUES ('" + question + "', '" + answer + "', '" + wrong1 + "', '" + wrong2 + "', '" + wrong3 + "')");
		res = executeSQL ("SELECT id FROM questions WHERE question = '" + question + "'");

		oss << "Questao criada com o ID: " << PQgetvalue (res, 0, 0);
	
		data = oss.str();
		writeline (socketfd, data);
	}
}

void cmd_create (int socketfd, string &line)
{
	string
		comando,
		time,
		data;
	istringstream
		iss (line);
	
	iss >> comando >> time;
	
	ostringstream
		oss1,
		oss2,
		oss3;

	oss1 << "INSERT INTO games (creator_id, questions, time) VALUES (" << curr_user.id << ", 0, " << time << ")";
	executeSQL (oss1.str());
	
	oss2 << "SELECT id FROM games WHERE creator_id = " << curr_user.id << " AND questions = 0 AND time = " << time << "";	
	PGresult* res = executeSQL (oss2.str());
	oss3 << "Jogo criado com o ID: " << PQgetvalue (res, 0, 0);
	
	data = oss3.str();
	writeline (socketfd, data);
}

void cmd_insert (int socketfd, string &line)
{
	string
		comando,
		game_id,
		question_id;
	istringstream
		iss (line);
	ostringstream
		oss1,
		oss2;
	PGresult*
		res;
	int
		question_nr;
	
	iss >> comando >> game_id >> question_id;
	
	res = executeSQL ("SELECT * FROM questions WHERE id = '" + question_id + "'");
	if (PQntuples (res) == 0)
	{
		writeline (socketfd, "Pergunta nao encontrado.");
		return ;
	}
	
	res = executeSQL ("SELECT questions FROM games WHERE id = '" + game_id + "'");
	if (PQntuples (res) == 0)
	{
		writeline (socketfd, "Jogo nao encontrado.");
		return ;
	}
	else
	{
		question_nr = 1 + atoi (PQgetvalue (res, 0, 0));
		oss1 << "INSERT INTO gamequestions (game_id, question_nr, question_id) VALUES (" << game_id << ", " << question_nr << ", " << question_id << ")";
		executeSQL (oss1.str());
		
		oss2 << "UPDATE games SET questions = " << question_nr << " WHERE id = " << game_id;
		executeSQL (oss2.str());
	}
}

void cmd_listusers (int socketfd)
{
	ostringstream
		oss;
	string
		data;
	
	PGresult* res = executeSQL ("SELECT * FROM players");
	for (int i = 0; i < PQntuples (res); i++)
	{
		oss << "Id: " << PQgetvalue (res, i, 0) << endl << "Nome: " << PQgetvalue (res, i, 1) << endl << endl;
	}

	data = oss.str();
	writeline (socketfd, data);
}

/* FUNÇOES DO JOGO */

/* Trata de receber dados de um cliente cujo socketid foi passado como parâmetro */
void* cliente (void* args)
{
	int
		socketfd = *(int*) args;
	string
		line;

	clients.insert (socketfd);

	cout << "Client connected: " << socketfd << endl;

	writeline (socketfd, curr_user.name + "> ");
	while (readline (socketfd, line))
	{		
		if (line.find ("\\help") == 0)
			cmd_help (socketfd);
		else if (line.find ("\\register") == 0)
			cmd_register (socketfd, line);
		else if (line.find ("\\login") == 0)
			cmd_login (socketfd, line);
		else if (line.find ("\\logout") == 0)
			cmd_logout (socketfd);
		else if (line.find ("\\question") == 0)
			cmd_question (socketfd, line);
		else if (line.find ("\\create") == 0)
			cmd_create (socketfd, line);
		else if (line.find ("\\insert") == 0)
			cmd_insert (socketfd, line);
		else if (line.find ("\\listusers") == 0)
			cmd_listusers (socketfd);

		writeline (socketfd, curr_user.name + "> ");
	}

	cout << "Client disconnected: " << socketfd << endl;
	clients.erase (socketfd);

	// Fecha o socket
	close (socketfd);
}

int main (int argc, char *argv[])
{
	// Estruturas de dados
	int
		socketfd,
		newsocketfd,
		port = atoi (argv[1]);
	socklen_t
		client_addr_length;
	struct sockaddr_in
		serv_addr,
		cli_addr;

	cout << "Port: " << port << endl;

	initDB ();
	empty_curr_user ();

	// Inicializar o socket
	// AF_INET:			para indicar que queremos usar IP
	// SOCK_STREAM:		para indicar que queremos usar TCP
	// socketfd:			id do socket principal do servidor
	// Se retornar < 0 ocorreu um erro
	socketfd = socket (AF_INET, SOCK_STREAM, 0);
	if (socketfd < 0)
	{
		cout << "Error creating socket" << endl;
		exit(-1);
	}

	// Criar a estrutura que guarda o endereço do servidor
	// bzero:		apaga todos os dados da estrutura (coloca a 0's)
	// AF_INET:		endereço IP
	// INADDR_ANY:	aceitar pedidos para qualquer IP
	bzero ((char *) &serv_addr, sizeof (serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons (port);

	// Fazer bind do socket. Apenas nesta altura é que o socket fica ativo mas ainda não estamos a tentar receber ligações.
	// Se retornar < 0 ocorreu um erro
	int res = bind (socketfd, (struct sockaddr *) &serv_addr, sizeof (serv_addr));
	if (res < 0)
	{
		cout << "Error binding to socket" << endl;
		exit(-1);
	}

	// Indicar que queremos escutar no socket com um backlog de 5 (podem ficar até 5 ligações pendentes antes de fazermos accept)
	listen(socketfd, 5);

	while (true)
	{
		// Aceitar uma nova ligação. O endereço do cliente fica guardado em 
		// cli_addr:	endereço do cliente
		// newsocketfd:	id do socket que comunica com este cliente */
		client_addr_length = sizeof (cli_addr);
		newsocketfd = accept (socketfd, (struct sockaddr *) &cli_addr, &client_addr_length);
		
		// Criar uma thread para tratar dos pedidos do novo cliente
		pthread_t thread;
		pthread_create (&thread, NULL, cliente, &newsocketfd);
	}

	closeDB ();
	close (socketfd);
	return 0; 
}