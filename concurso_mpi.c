#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_QUESTOES 30
#define CARGO_ALVO "0603"
#define MAX_CANDIDATOS 9999
#define NOTA_CORTE 70.0

typedef struct {
    char id[10];
    double nota_portugues;
    double nota_Matematica;
    double nota_especifica;
    double media;
} Candidato;

char gabarito[NUM_QUESTOES][2];
char respostas[MAX_CANDIDATOS][NUM_QUESTOES][2];
char candidatos[MAX_CANDIDATOS][10];
int total_candidatos = 0;
int total_processos, id_processo;

void split(const char *str, char result[NUM_QUESTOES][2]) {
    char temp[300];
    strncpy(temp, str, sizeof(temp) - 1); 
    temp[sizeof(temp) - 1] = '\0'; 

    char *token = strtok(temp, ",");
    for (int i = 0; token != NULL && i < NUM_QUESTOES; i++) {
        strncpy(result[i], token, sizeof(result[i]) - 1);
        result[i][sizeof(result[i]) - 1] = '\0'; 
        token = strtok(NULL, ",");
    }
}

void carregar_gabarito() {
    FILE *file = fopen("./dados/gabarito.csv", "r");
    
    if (file == NULL) {
        perror("Erro ao abrir gabarito.csv");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    char linha[300] = {0}; 

    if (fgets(linha, sizeof(linha), file) != NULL) {
        split(linha, gabarito);
    }

    fclose(file);
}


void carregar_respostas() {
    FILE *file = fopen("./dados/respostas.csv", "r");
    if (!file) {
        perror("Erro ao abrir respostas.csv"); 
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    char linha[400];
    while (fgets(linha, sizeof(linha), file)) {
        char id[10] = "", cargo[5] = "", questoes[300] = "";

        if (sscanf(linha, "\"%9[^\"]\",\"%4[^\"]\",\"%299[^\"]\"", id, cargo, questoes) == 3) {
            if (strcmp(cargo, CARGO_ALVO) == 0 && total_candidatos < MAX_CANDIDATOS) {
                strncpy(candidatos[total_candidatos], id, sizeof(candidatos[total_candidatos]) - 1);
                candidatos[total_candidatos][sizeof(candidatos[total_candidatos]) - 1] = '\0';
                split(questoes, respostas[total_candidatos]);
                total_candidatos++;
            }
        }
    }

    fclose(file);
}


void calcular_acertos(int *acertos) {
    memset(acertos, 0, NUM_QUESTOES * sizeof(int));

    for (int i = 0; i < total_candidatos; i++) {
        for (int j = 0; j < NUM_QUESTOES; j++) {
            acertos[j] += (strcmp(respostas[i][j], gabarito[j]) == 0);
        }
    }
}

void calcular_notas(Candidato *resultados, double *pontuacoes) {
    for (int i = 0; i < total_candidatos; i++) {
        // Inicializa as notas do candidato
        double notas[3] = {0, 0, 0};

        // Calcula a pontuação em cada grupo de questões
        for (int j = 0; j < NUM_QUESTOES; j++) {
            if (strcmp(respostas[i][j], gabarito[j]) == 0) {
                notas[j / 10] += pontuacoes[j]; // Usa divisão para determinar o grupo automaticamente
            }
        }

        // Atribui os valores ao candidato
        resultados[i].nota_portugues = notas[0];
        resultados[i].nota_Matematica = notas[1];
        resultados[i].nota_especifica = notas[2];

        // Calcula a média final
        resultados[i].media = (notas[0] + notas[1] + notas[2]) / 3;

        // Copia o ID do candidato
        strcpy(resultados[i].id, candidatos[i]);
    }
}


int comparar_candidatos(const void *a, const void *b) {
    Candidato *c1 = (Candidato *)a;
    Candidato *c2 = (Candidato *)b;

    if (c2->media > c1->media) return 1;   // Retorna 1 se c2 tiver média maior (ordem decrescente)
    if (c2->media < c1->media) return -1;  // Retorna -1 se c1 tiver média maior
    return 0;                               // Retorna 0 se forem iguais
}

void calcular_pontuacoes_paralelo(double *pontuacoes) {
    int acertos[NUM_QUESTOES] = {0};

    // Determina quais questões este processo será responsável por calcular
    int questoes_por_processo = NUM_QUESTOES / total_processos;
    int inicio = id_processo * questoes_por_processo;
    int fim = (id_processo == total_processos - 1) ? NUM_QUESTOES : inicio + questoes_por_processo;

    // Cada processo calcula os acertos para seu subconjunto de questões
    for (int i = 0; i < total_candidatos; i++) {
        for (int j = inicio; j < fim; j++) {
            if (strcmp(respostas[i][j], gabarito[j]) == 0) {
                acertos[j]++;
            }
        }
    }

    // Processo 0 recebe os resultados de todos os processos
    int acertos_globais[NUM_QUESTOES] = {0};
    MPI_Reduce(acertos, acertos_globais, NUM_QUESTOES, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    // Processo 0 calcula as pontuações
    if (id_processo == 0) {
        int max_acertos = 0;
        for (int i = 0; i < NUM_QUESTOES; i++) {
            if (acertos_globais[i] > max_acertos) {
                max_acertos = acertos_globais[i];
            }
        }

        double grau_dificuldade[NUM_QUESTOES] = {0};
        double soma_graus[3] = {0};

        for (int i = 0; i < NUM_QUESTOES; i++) {
            grau_dificuldade[i] = (acertos_globais[i] > 0) ? ((double)max_acertos / acertos_globais[i]) * 4 : 0;
            soma_graus[i / 10] += grau_dificuldade[i];
        }

        for (int i = 0; i < NUM_QUESTOES; i++) {
            int grupo = i / 10;
            pontuacoes[i] = (soma_graus[grupo] > 0) ? (grau_dificuldade[i] / soma_graus[grupo]) * 100 : 0;
        }
    }

    // Processo 0 distribui as pontuações calculadas para todos os processos
    MPI_Bcast(pontuacoes, NUM_QUESTOES, MPI_DOUBLE, 0, MPI_COMM_WORLD);
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &total_processos);
    MPI_Comm_rank(MPI_COMM_WORLD, &id_processo);

    double tempo_inicio = MPI_Wtime(); // Início da medição

    if (id_processo == 0) {
        carregar_gabarito();
        carregar_respostas();
    }

    MPI_Bcast(gabarito, sizeof(gabarito), MPI_CHAR, 0, MPI_COMM_WORLD);
    MPI_Bcast(&total_candidatos, 1, MPI_INT, 0, MPI_COMM_WORLD);

    double pontuacoes[NUM_QUESTOES];
    calcular_pontuacoes_paralelo(pontuacoes);

    Candidato resultados[MAX_CANDIDATOS];
    calcular_notas(resultados, pontuacoes);

    if (id_processo == 0) {
        qsort(resultados, total_candidatos, sizeof(Candidato), comparar_candidatos);

        printf("\n--- NOTA DE CADA QUESTÃO ---\n");
        for (int i = 0; i < NUM_QUESTOES; i++) {
            printf("Questão %d: %.2f\n", i + 1, pontuacoes[i]);
        }

        printf("\n--- NOTA DE CADA CANDIDATO ---\n");
        for (int i = 0; i < total_candidatos; i++) {
            printf("Candidato  %s - LP: %.2f, ML: %.2f, ESP: %.2f, Média Final: %.2f\n",
                   resultados[i].id, resultados[i].nota_portugues, resultados[i].nota_Matematica, resultados[i].nota_especifica, resultados[i].media);
        }

        printf("\n--- CLASSIFICADOS ---\n");
            int classificados = 0;
            for (int i = 0; i < total_candidatos; i++) {
                if (resultados[i].media >= NOTA_CORTE) {
                    printf("%d. %s - Média: %.2f\n", classificados + 1, resultados[i].id, resultados[i].media);
                    classificados++;
                }
        }


        if (classificados == 0) {
            printf("Nenhum candidato foi classificado.\n");
        }

        double tempo_fim = MPI_Wtime(); // Fim da medição
        printf("\nTempo total de execução: %.6f segundos\n", tempo_fim - tempo_inicio);
    }

    MPI_Finalize();
    return 0;
}