
#include <stdio.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <time.h>
#include <errno.h>

int esperarEvento (int fd) {
    fd_set conjunto;

    FD_ZERO(&conjunto);

    FD_SET(0, &conjunto); // o 0 é do teclado
    FD_SET(fd, &conjunto); //aqui colocando o pts

    int r = select(fd+1, &conjunto, NULL, NULL, NULL);

    if (r < 0) {
        return -1;
    }

    int retorno = 0;

    if (FD_ISSET(0, &conjunto)) {
        retorno++;
    }

    if (FD_ISSET(fd, &conjunto)) {
        retorno += 2;
    }

    return retorno;
}

char *lerArgumentos (int argc, char *argv[]) {
    if (argc < 2) {
        return NULL;
    } 

    return argv[1];
}

int abrirSerial (char *caminho) {
    return open(caminho, O_RDWR | O_NOCTTY);
}

FILE *abrirLog () {
    return fopen ("conversa.txt", "a");
}

void encerrar (int fd, FILE *log) {

    close (fd);
    if (log != NULL) {
        fclose (log);
    }
}

int configurarSerial (int fd) {

    struct termios cfg;

    if (tcgetattr (fd, &cfg) == -1) {
        return -1;
    }

    cfmakeraw (&cfg);

    if (tcsetattr (fd, TCSANOW, &cfg) == -1) {
        return -1;
    }

    return 0;

}

void registrarNoLog (FILE *log, char *modo, char *mensagem) {

    time_t agora = time (NULL);
    struct tm *t = localtime(&agora);
    char hora[32];
    strftime (hora, sizeof(hora), "%d/%m/%Y %H:%M:%S", t);
    fprintf (log, "[%s] %s: %s\n", hora, modo, mensagem);
    fflush (log);

}

int tratarTeclado (int fd, FILE *log) {

    char buf [256];

    ssize_t n = read (0, buf, sizeof (buf) - 1);

    if (n <= 0) {
        write (fd, "Sair\n", strlen("Sair\n")); //assim o outro sabe que saímos, por exemplo com um ctrl+d
        return 0;
    }

    buf[n] = '\0';

    if (strcmp(buf, "Sair\n") == 0) { // o \n é pois buf termina com /n
        int sair = write (fd, buf, n);
        if (sair == -1) {
            perror ("Erro ao sair");
        }
        return 0;
    }

    int escrever = write (fd, buf, n);

    if (escrever == -1) {
        perror ("Erro ao escrever no parceiro.");
    }
    buf[strcspn(buf, "\n")] = '\0';
    registrarNoLog (log, "ENVIADA", buf);

    return 1;

}

void enviarHistorico (int fd) {
    FILE *conversa = fopen ("conversa.txt", "r"); // pra poder ler

    if (conversa == NULL) {

        char erro[256];
        snprintf (erro, sizeof(erro), "#ERRO Erro ao ler histórico: %s\n", strerror(errno));
        int escreverLog = write (fd, erro, strlen(erro));
        if (escreverLog == -1) {
            perror ("Erro ao enviar aviso de falha de leitura ao parceiro.");
        }
        return;

    }

    char linha[256];

    int inicioLog = write (fd, "#INICIO LOG\n", strlen("#INICIO LOG\n"));
    if (inicioLog == -1) {
        perror ("Erro ao iniciar log");
    }
    while (fgets(linha, sizeof (linha), conversa) != NULL) {
        int duranteLog = write (fd, linha, strlen(linha));
        if (duranteLog == -1) {
            perror ("Erro ao escrever no log");
        }
    }
    int fecharLog = write (fd, "#FIM LOG\n", strlen("#FIM LOG\n"));
    if (fecharLog == -1) {
        perror ("Erro ao fechar log");
    }
    fclose(conversa);
}

int processarLinha (int fd, FILE *log, char *linha) {

    static int recebendoHistorico = 0;

    if (recebendoHistorico) {
        if (strcmp(linha, "#FIM LOG") == 0) {
            recebendoHistorico = 0;
        } else {
            printf ("[histórico]: %s\n", linha);
        }

        return 1;
    }

    if (strcmp(linha, "#INICIO LOG") == 0) {
        recebendoHistorico = 1;
        return 1;
    }

    if (strncmp(linha, "#ERRO", 5) == 0) {
        printf ("Erro no parceiro: %s\n", linha + 6);
        return 1;
    }

    if (strcmp(linha, "Sair") == 0) {
        fprintf (stdout, "Parceiro decidiu sair\n");
        return 0;
    }

    if (strcmp (linha, "LOG") == 0) {
        enviarHistorico (fd);
        return 1;
    }
    
    printf ("%s\n", linha);
    registrarNoLog (log, "RECEBIDA", linha);
    return 2;

}

int tratarSerial (int fd, FILE *log) {
    
    static char pendente [1024];

    static size_t tam = 0;

    ssize_t n = read (fd, pendente + tam, sizeof (pendente) - 1 - tam);

    if (n <= 0) {
        if (n == 0) {
            printf("O parceiro desconectou\n");
        }
        else {
            perror ("Erro na leitura da serial.");
        }
        return 0;
    }

    tam = tam + n;

    pendente[tam] = '\0';


    while (1) {

        char *fim = strchr(pendente, '\n');

        if (fim == NULL) {
            return 1;
        }

        *fim = '\0';

        int resultado;

        resultado = processarLinha (fd, log, pendente);

        if (resultado == 0) {
            return 0;
        }

        size_t tamLinha = (fim - pendente) + 1;
        memmove (pendente, fim + 1, tam - tamLinha + 1);
        tam = tam - tamLinha;
        
    }

    return 1;
}

int main (int argc, char *argv[]) {

    char *caminho = lerArgumentos (argc, argv);

    if (caminho == NULL) {
        fprintf (stderr, "Argumento com caminho não informado\n"); // mostrando erro na saida de erros padrão
        return 1;
    }
    
    int fdSerial = abrirSerial (caminho);

    if (fdSerial < 0) {
        perror ("Erro ao abrir caminho");
        return 1;
    }

    if (configurarSerial (fdSerial) < 0) {
        perror ("Erro ao configurar serial");
        encerrar (fdSerial, NULL);
        return 1;
    }

    FILE* log = abrirLog ();

    if (log == NULL) {
        perror ("Erro ao criar arquivo de Log");
        encerrar (fdSerial, log);
        return 1;
    }

    int conversando = 1;

    while (conversando) {

        int evento = esperarEvento(fdSerial); //select

        if (evento == -1) {
            conversando = 0;
            perror ("Erro no select interno");
            break;
        }

        if (evento == 1 || evento == 3) { //no caso é teclado, entrou pela entrada
            conversando = tratarTeclado(fdSerial, log);
        }

        if (evento == 2 || (evento == 3 && conversando)) { // no caso é serial, foi pra saída
            conversando = tratarSerial(fdSerial, log);
        }
    }

    encerrar (fdSerial, log);

    return 0;
}