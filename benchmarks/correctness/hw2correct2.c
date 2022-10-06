#include <stdio.h>

int main(){
    int A[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int B[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    int i, j;
    j = 0;
    for(i = 0; i < 10; i++) {
        B[i] = A[j] * 13 + 4 + i;
        if(i % 8 == 0)
            j = i;
        printf("%d\n", B[i]);
    }
    return 0;
}

//int main(){
//	int A[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
//	int B[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//	int i, j;
//	j = 0;
//	for(i = 0; i < 100000000; i++) {
//  		B[i%10] = A[j] * 13 + 4 + i;
//  		if(i % 8 == 0)
//  			j = i%10;
//		printf("%d\n", B[i%10]);
//	}
//	return 0;
//}
