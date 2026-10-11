# 3DS-Stream

[English](README.md) · **Português**

O cliente de Nintendo 3DS do [PSPStream](https://github.com/k7vinilstorage/PSP-Stream):
a tela e o som do PC no 3DS pelo Wi-Fi, e os botões do 3DS de volta para o
PC, com o mesmo servidor que o PSP usa.

**Estado: fase 0, testes no hardware.** Ainda não há cliente. O plano é o
[3DS Port Plan](https://github.com/k7vinilstorage/PSP-Stream/wiki/3DS-Port-Plan)
da wiki do PSP-Stream: H.264 pelo decoder de hardware do New 3DS (MVD), JPEG
no Old 3DS, 400x240 na tela de cima, protocolo v5 primeiro.

## O probe

`probe/` mede no console o que o plano precisa saber: se o MVD decodifica os
pacotes H.264 do servidor e quantos frames ele segura, a velocidade do JPEG
por software e do conversor Y2R, a vazão e o ping do Wi-Fi com o servidor de
verdade, e o som pelo NDSP.

Para rodar:

1. Pegue o `3ds-stream-probe.3dsx` no último build do workflow **build**
   (artefato `3ds-stream-probe`), copie para `sdmc:/3ds/` e abra pelo
   Homebrew Launcher.
2. No PC: `pspstream --source test --size 400x240`. Se a busca não achar o
   PC, coloque `server=IP-do-PC` em `sdmc:/3ds/3ds-stream/server.txt`.
3. Para o som: o dump do DSP em `sdmc:/3ds/dspfirm.cdc`.
4. Aperte **A**. Leva cerca de um minuto. O resultado fica em
   `sdmc:/3ds/3ds-stream/probe_result.txt` (no New 3DS, mande também os
   `mvd_dump_*.bin`).

Se o console travar num passo, desligue e rode de novo: o passo vai para
`crashed.txt` e é pulado. Detalhes de cada medida: [docs/Probe.md](docs/Probe.md)
(em inglês).

## Licença

MIT, como o PSP-Stream.
