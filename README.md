# `edu_pbuf`: fila educacional de alertas para Linux

`edu_pbuf` é um pseudo device driver educacional para o Kernel Linux. Ele cria
o character device `/dev/edu_pbuf` e simula uma fila de alertas do sistema para
aplicações de monitoramento em user space.

A ideia é simples: um processo publica alertas escrevendo em `/dev/edu_pbuf`, e
outro processo lê ou espera esses alertas pelo mesmo device.

Tecnicamente, o requisito principal do projeto está no caminho de leitura e
escrita:

- `write()` usa `copy_from_user()` para copiar o alerta de user space para um
  buffer em memória do kernel;
- `read()` usa `copy_to_user()` para copiar os alertas pendentes do kernel de
  volta para user space.

Além do buffer mínimo, o driver demonstra recursos comuns em drivers reais:

- comandos `ioctl()` para consulta e configuração estruturada;
- atributos `sysfs` para configuração simples e diagnóstico;
- modo append para acumular alertas;
- modo clear-on-read para consumir alertas após leitura;
- leitura bloqueante para um monitor esperar novos alertas;
- suporte a `poll/select`;
- estatísticas internas de uso.

## O que o driver demonstra

- Carregamento dinâmico de módulo com `insmod`, `lsmod` e `rmmod`.
- Integração in-tree com `Kconfig` e `Makefile`.
- Organização em múltiplos arquivos: core, file operations e `sysfs`.
- Registro de character device com `alloc_chrdev_region()` e `cdev`.
- Criação automática de `/dev/edu_pbuf` com `class_create()` e
  `device_create()`.
- Transferência segura de dados entre user space e kernel space com
  `copy_from_user()` e `copy_to_user()`.
- Configuração em tempo de execução com `ioctl()`.
- Modos append, clear-on-read e blocking-read.
- Prontidão de leitura/escrita com `poll/select` e `wait_queue`.
- Atributos `sysfs` para configuração e diagnóstico.
- Estatísticas internas expostas por `ioctl()` e `sysfs`.

## Organização do módulo

O módulo final continua sendo `edu_pbuf.ko`, mas a implementação foi separada em
arquivos menores:

```text
edu_pbuf_core.c      inicialização, saída, cdev, /dev/edu_pbuf e sysfs group
edu_pbuf_fops.c      open, release, read, write, ioctl e poll
edu_pbuf_sysfs.c     atributos limit, flags, length, stats e clear
edu_pbuf_internal.h  estado compartilhado e declarações internas
```

## Arquivos do repositório

```text
drivers/lkcamp/Kconfig
drivers/lkcamp/Makefile
drivers/lkcamp/edu_pbuf_core.c
drivers/lkcamp/edu_pbuf_fops.c
drivers/lkcamp/edu_pbuf_sysfs.c
drivers/lkcamp/edu_pbuf_internal.h
include/uapi/linux/edu_pbuf.h
tools/testing/selftests/edu_pbuf/Makefile
tools/testing/selftests/edu_pbuf/edu_pbuf_test.c
kernel-integration.patch
```

Este repositório não inclui a pasta `docs/` do projeto principal.

## Aplicando em uma árvore do Kernel Linux

A partir da raiz de uma árvore compatível do Kernel Linux:

```sh
cp -r /path/to/edu-pbuf-device-driver/drivers/lkcamp drivers/
cp /path/to/edu-pbuf-device-driver/include/uapi/linux/edu_pbuf.h include/uapi/linux/
mkdir -p tools/testing/selftests/edu_pbuf
cp /path/to/edu-pbuf-device-driver/tools/testing/selftests/edu_pbuf/* \
    tools/testing/selftests/edu_pbuf/
git apply /path/to/edu-pbuf-device-driver/kernel-integration.patch
```

## Compilação

Para compilar pela própria árvore do kernel:

```sh
./scripts/config -m EDU_PBUF
make olddefconfig
make -j"$(nproc)" drivers/lkcamp/edu_pbuf.ko
make -C tools/testing/selftests/edu_pbuf
```

Para compilar contra o kernel que está rodando no host:

```sh
make -C /lib/modules/$(uname -r)/build \
  M=$PWD/drivers/lkcamp \
  CONFIG_EDU_PBUF=m \
  KCFLAGS=-I$PWD/include \
  modules

make -C tools/testing/selftests/edu_pbuf
```

## Execução

Use uma VM ou sistema rodando a mesma versão/configuração do kernel usada para
compilar o módulo. Se o `vermagic` do módulo não corresponder ao `uname -r`, o
`insmod` pode falhar com `Invalid module format`.

Carregar o módulo:

```sh
sudo insmod drivers/lkcamp/edu_pbuf.ko capacity=4096
lsmod | grep edu_pbuf
ls -l /dev/edu_pbuf
sudo dmesg -T | tail -n 20
```

Publicar e ler um alerta:

```sh
printf 'ALERTA temperatura=82 origem=demo severidade=alta\n' | sudo tee /dev/edu_pbuf
sudo cat /dev/edu_pbuf
```

## Demonstração como fila de alertas

Ativar append para acumular alertas:

```sh
echo 0x1 | sudo tee /sys/class/edu_pbuf/edu_pbuf/flags
printf 'ALERTA cpu=91 origem=append\n' | sudo tee /dev/edu_pbuf
printf 'ALERTA disco=88 origem=append\n' | sudo tee /dev/edu_pbuf
sudo cat /dev/edu_pbuf
```

Ativar leitura bloqueante e limpeza após leitura:

```sh
echo 4096 | sudo tee /sys/class/edu_pbuf/edu_pbuf/limit >/dev/null
echo 0x6 | sudo tee /sys/class/edu_pbuf/edu_pbuf/flags >/dev/null
echo clear | sudo tee /sys/class/edu_pbuf/edu_pbuf/clear >/dev/null
cat /sys/class/edu_pbuf/edu_pbuf/limit
cat /sys/class/edu_pbuf/edu_pbuf/flags
cat /sys/class/edu_pbuf/edu_pbuf/length
```

Em um terminal, deixar um monitor esperando alertas:

```sh
sudo cat /dev/edu_pbuf
```

Esse comando deve ficar parado até outro terminal publicar um alerta.

Em outro terminal, publicar alertas:

```sh
printf 'ALERTA cpu=95 origem=terminal2 severidade=alta\n' | sudo tee /dev/edu_pbuf >/dev/null
printf 'ALERTA memoria=87 origem=terminal2 severidade=media\n' | sudo tee /dev/edu_pbuf >/dev/null
```

O terminal do monitor acorda quando cada alerta chega. Use `Ctrl+C` para parar o
monitor.

Se o mesmo alerta aparecer repetido muitas vezes, pare o monitor com `Ctrl+C` e
confirme se `flags` está em `0x6`. Esse valor ativa `CLEAR_ON_READ` e
`BLOCKING_READ`; sem ele, o monitor pode reler o mesmo alerta em loop.

## Teste com `ioctl()`

```sh
sudo tools/testing/selftests/edu_pbuf/edu_pbuf_test /dev/edu_pbuf
```

O selftest consulta informações, limpa a fila, ativa flags, escreve alertas,
usa `poll()`, lê os dados, altera o limite lógico e consulta estatísticas.

## Inspeção por `sysfs`

```sh
ls -l /sys/class/edu_pbuf/edu_pbuf/
cat /sys/class/edu_pbuf/edu_pbuf/limit
cat /sys/class/edu_pbuf/edu_pbuf/flags
cat /sys/class/edu_pbuf/edu_pbuf/length
cat /sys/class/edu_pbuf/edu_pbuf/stats
echo clear | sudo tee /sys/class/edu_pbuf/edu_pbuf/clear
```

## Remoção

```sh
sudo rmmod edu_pbuf
sudo dmesg -T | tail -n 20
```

## Resumo

Para o usuário, `edu_pbuf` é um dispositivo virtual em `/dev/edu_pbuf` que
funciona como uma fila educacional de alertas. Ele permite publicar alertas,
armazená-los em memória do kernel, recuperá-los por leitura, esperar novos
eventos e configurar o comportamento por `ioctl()` ou `sysfs`.
