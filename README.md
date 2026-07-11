# `edu_chat`: sala de chat local para Linux

`edu_chat` e um pseudo device driver educacional para o Kernel Linux. Ele cria
o character device `/dev/edu_chat` e simula uma sala de chat local onde
processos em user space podem trocar mensagens.

A ideia e simples: um processo envia uma mensagem escrevendo em `/dev/edu_chat`,
e outro processo le a conversa pelo mesmo device.

Tecnicamente, o requisito principal do projeto esta no caminho de leitura e
escrita:

- `write()` usa `copy_from_user()` para copiar a mensagem de user space para um
  buffer em memoria do kernel;
- `read()` usa `copy_to_user()` para copiar as mensagens pendentes do kernel de
  volta para user space.

Alem do buffer minimo, o driver demonstra recursos comuns em drivers reais:

- comandos `ioctl()` para consulta e configuracao estruturada;
- atributos `sysfs` para configuracao simples e diagnostico;
- modo append para acumular mensagens (historico de chat);
- modo clear-on-read para consumir mensagens apos leitura;
- leitura bloqueante para um leitor esperar novas mensagens;
- suporte a `poll/select`;
- estatisticas internas de uso.

## O que o driver demonstra

- Carregamento dinamico de modulo com `insmod`, `lsmod` e `rmmod`.
- Integracao in-tree com `Kconfig` e `Makefile`.
- Organizacao em multiplos arquivos: core, file operations e `sysfs`.
- Registro de character device com `alloc_chrdev_region()` e `cdev`.
- Criacao automatica de `/dev/edu_chat` com `class_create()` e
  `device_create()`.
- Transferencia segura de dados entre user space e kernel space com
  `copy_from_user()` e `copy_to_user()`.
- Configuracao em tempo de execucao com `ioctl()`.
- Modos append, clear-on-read e blocking-read.
- Prontidao de leitura/escrita com `poll/select` e `wait_queue`.
- Atributos `sysfs` para configuracao e diagnostico.
- Estatisticas internas expostas por `ioctl()` e `sysfs`.

## Organizacao do modulo

O modulo final continua sendo `edu_chat.ko`, mas a implementacao foi separada em
arquivos menores:

```text
edu_chat_core.c      inicializacao, saida, cdev, /dev/edu_chat e sysfs group
edu_chat_fops.c      open, release, read, write, ioctl e poll
edu_chat_sysfs.c     atributos limit, flags, length, stats e clear
edu_chat_internal.h  estado compartilhado e declaracoes internas
```

## Arquivos do repositorio

```text
drivers/lkcamp/Kconfig
drivers/lkcamp/Makefile
drivers/lkcamp/edu_chat_core.c
drivers/lkcamp/edu_chat_fops.c
drivers/lkcamp/edu_chat_sysfs.c
drivers/lkcamp/edu_chat_internal.h
include/uapi/linux/edu_chat.h
tools/testing/selftests/edu_chat/Makefile
tools/testing/selftests/edu_chat/edu_chat_test.c
kernel-integration.patch
```

Este repositorio nao inclui a pasta `docs/` do projeto principal.

## Aplicando em uma arvore do Kernel Linux

A partir da raiz de uma arvore compativel do Kernel Linux:

```sh
cp -r /path/to/edu-pbuf-device-driver/drivers/lkcamp drivers/
cp /path/to/edu-pbuf-device-driver/include/uapi/linux/edu_chat.h include/uapi/linux/
mkdir -p tools/testing/selftests/edu_chat
cp /path/to/edu-pbuf-device-driver/tools/testing/selftests/edu_chat/* \
    tools/testing/selftests/edu_chat/
git apply /path/to/edu-pbuf-device-driver/kernel-integration.patch
```

## Compilacao

Para compilar pela propria arvore do kernel:

```sh
./scripts/config -m EDU_CHAT
make olddefconfig
make -j"$(nproc)" drivers/lkcamp/edu_chat.ko
make -C tools/testing/selftests/edu_chat
```

Para compilar contra o kernel que esta rodando no host:

```sh
make -C /lib/modules/$(uname -r)/build \
  M=$PWD/drivers/lkcamp \
  CONFIG_EDU_CHAT=m \
  KCFLAGS=-I$PWD/include \
  modules

make -C tools/testing/selftests/edu_chat
```

## Execucao

Use uma VM ou sistema rodando a mesma versao/configuracao do kernel usada para
compilar o modulo. Se o `vermagic` do modulo nao corresponder ao `uname -r`, o
`insmod` pode falhar com `Invalid module format`.

Carregar o modulo:

```sh
sudo insmod drivers/lkcamp/edu_chat.ko capacity=4096
lsmod | grep edu_chat
ls -l /dev/edu_chat
sudo dmesg -T | tail -n 20
```

Enviar e ler uma mensagem:

```sh
printf 'rodrigo: oi pessoal, tudo bem?\n' | sudo tee /dev/edu_chat
sudo cat /dev/edu_chat
```

## Demonstracao como sala de chat

Ativar append para acumular mensagens:

```sh
echo 0x1 | sudo tee /sys/class/edu_chat/edu_chat/flags
printf 'rodrigo: bom dia!\n' | sudo tee /dev/edu_chat
printf 'ana: bom dia rodrigo!\n' | sudo tee /dev/edu_chat
sudo cat /dev/edu_chat
```

Ativar leitura bloqueante e limpeza apos leitura:

```sh
echo 4096 | sudo tee /sys/class/edu_chat/edu_chat/limit >/dev/null
echo 0x6 | sudo tee /sys/class/edu_chat/edu_chat/flags >/dev/null
echo clear | sudo tee /sys/class/edu_chat/edu_chat/clear >/dev/null
cat /sys/class/edu_chat/edu_chat/limit
cat /sys/class/edu_chat/edu_chat/flags
cat /sys/class/edu_chat/edu_chat/length
```

Em um terminal, deixar um leitor esperando mensagens:

```sh
sudo cat /dev/edu_chat
```

Esse comando deve ficar parado ate outro terminal enviar uma mensagem.

Em outro terminal, enviar mensagens:

```sh
printf 'rodrigo: oi ana, tudo bem?\n' | sudo tee /dev/edu_chat >/dev/null
printf 'ana: oi rodrigo! tudo otimo\n' | sudo tee /dev/edu_chat >/dev/null
```

O terminal do leitor acorda quando cada mensagem chega. Use `Ctrl+C` para parar
o leitor.

Se a mesma mensagem aparecer repetida muitas vezes, pare o leitor com `Ctrl+C` e
confirme se `flags` esta em `0x6`. Esse valor ativa `CLEAR_ON_READ` e
`BLOCKING_READ`; sem ele, o leitor pode reler a mesma mensagem em loop.

## Teste com `ioctl()`

```sh
sudo tools/testing/selftests/edu_chat/edu_chat_test /dev/edu_chat
```

O selftest consulta informacoes, limpa o historico, ativa flags, escreve
mensagens, usa `poll()`, le os dados, altera o limite logico e consulta
estatisticas.

## Inspecao por `sysfs`

```sh
ls -l /sys/class/edu_chat/edu_chat/
cat /sys/class/edu_chat/edu_chat/limit
cat /sys/class/edu_chat/edu_chat/flags
cat /sys/class/edu_chat/edu_chat/length
cat /sys/class/edu_chat/edu_chat/stats
echo clear | sudo tee /sys/class/edu_chat/edu_chat/clear
```

## Remocao

```sh
sudo rmmod edu_chat
sudo dmesg -T | tail -n 20
```

## Resumo

Para o usuario, `edu_chat` e um dispositivo virtual em `/dev/edu_chat` que
funciona como uma sala de chat local entre processos. Ele permite enviar
mensagens, armazena-las em memoria do kernel, recupera-las por leitura, esperar
novas mensagens e configurar o comportamento por `ioctl()` ou `sysfs`.
