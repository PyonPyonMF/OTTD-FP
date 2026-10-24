**First person view for OpenTTD.** Ride along in the cab of your trains, road vehicles, ships and aircraft, step out and walk around, get into a coach or a bus and ride along as a passenger, all in a 3D view drawn from the game's own graphics.

This is an **alpha**: an early test build based on the development version of OpenTTD (16.0). Expect rough edges, and please report what looks wrong.

*По-русски: [ниже](#по-русски).*

## 1. Download

Download **one file** for your computer:

| Computer | File |
|---|---|
| **Mac** (Apple Silicon and Intel, macOS 10.15 or newer) | [openttd-{VERSION}-macos.zip](https://github.com/{REPO}/releases/download/{VERSION}/openttd-{VERSION}-macos.zip) |
| **Windows** 10 or 11, 64-bit | [openttd-{VERSION}-windows-64bit.zip](https://github.com/{REPO}/releases/download/{VERSION}/openttd-{VERSION}-windows-64bit.zip) |
| **Linux**, 64-bit | [openttd-{VERSION}-linux-64bit.tar.xz](https://github.com/{REPO}/releases/download/{VERSION}/openttd-{VERSION}-linux-64bit.tar.xz) |

The same files are listed under **Assets** at the bottom of this page. You do not need the *Source code* files or `SHA256SUMS.txt`.

You do not need OpenTTD installed: graphics (OpenGFX), sounds (OpenSFX) and music (OpenMSX) are included.

## 2. Unpack and start

- **Mac:** double-click the zip, then double-click `OpenTTD.app` (you can move it to Applications first).
  The build is not signed by Apple, so the first time macOS says it cannot open it. Click *Done*, open *System Settings > Privacy & Security*, scroll down and click *Open Anyway* next to OpenTTD, then confirm. After that it opens normally.
- **Windows:** right-click the zip, choose *Extract All*, open the extracted folder and run `openttd.exe`.
  If a blue *Windows protected your PC* window appears, click *More info* and *Run anyway*.
- **Linux:** unpack it (`tar xf openttd-{VERSION}-linux-64bit.tar.xz`) and run `./openttd` in the unpacked folder.

It does not replace an OpenTTD you already have. Settings are shared with it.

## 3. Ride, walk and get in

1. Start or load a game and click any train, road vehicle, ship or aircraft to open its vehicle window.
2. Press the new button with the front of a locomotive, under "Details" on the right side. The **Cab view** window opens: the map follows the vehicle and looks ahead along the track or road, with a dashboard below.
3. Press **3D** for the view from the cab window. Hold the mouse button you scroll the map with on the view to look around; **Look ahead** looks straight ahead again.
4. When the train or road vehicle stands still, like at a station, press **Step out**. Walk with the **arrow keys** or **W A S D**, run with **Shift**, look around with the mouse.
5. Next to a coach or a bus that stands still, press **E** (or *Get in*): inside, walk along the aisle and through the doors to the next coaches, press **Space** to sit down by a window, and ride along. Press **E** at the next stop to get off. Next to a locomotive, **E** takes you into its cab.

The console (`~`) has `fp help` for more.

## Good to know

- **Savegames:** the first person view changes nothing in the game itself, but this build is based on the development version of OpenTTD, so its savegames open only in that version or newer.
- **Multiplayer** works only with players running the same alpha build. Nobody else sees you walking: the game does not know about you, and trains do not stop for you, they push you aside.
- The 3D view is drawn by the processor, without the graphics card. On a recent computer it runs smoothly; if it stutters, make the window smaller.
- Only the default graphics are tested. NewGRF vehicles and buildings are shown, but may look odd.
- Not yet: catenary, smoke, night, other vehicles moving smoothly, driving the train yourself.

Based on the cab view of [vaclavloubek/OpenTTD](https://github.com/vaclavloubek/OpenTTD/releases/tag/15.3-cabview-alpha1).
Graphics: OpenGFX (GPL v2). Sounds: OpenSFX (CC BY-SA 3.0, GPL v2 or CDDL 1.1). Music: OpenMSX (GPL v2). OpenTTD is licensed under the GPL v2; the source code of this build is in this repository.

---

## По-русски

**Вид от первого лица для OpenTTD.** Поездка в кабине поездов, автомобилей, судов и самолётов, прогулка пешком, поездка пассажиром в вагоне или автобусе — в 3D-виде из графики самой игры.

Это **альфа-версия**, первая пробная сборка на основе версии OpenTTD в разработке (16.0). Не всё доведено до конца; о том, что выглядит неправильно, пожалуйста, сообщайте.

**1. Скачайте один файл** для своего компьютера:

- **Mac** (Apple Silicon и Intel, macOS 10.15 и новее): [openttd-{VERSION}-macos.zip](https://github.com/{REPO}/releases/download/{VERSION}/openttd-{VERSION}-macos.zip)
- **Windows** 10 или 11, 64 бита: [openttd-{VERSION}-windows-64bit.zip](https://github.com/{REPO}/releases/download/{VERSION}/openttd-{VERSION}-windows-64bit.zip)
- **Linux**, 64 бита: [openttd-{VERSION}-linux-64bit.tar.xz](https://github.com/{REPO}/releases/download/{VERSION}/openttd-{VERSION}-linux-64bit.tar.xz)

Файлы *Source code* и `SHA256SUMS.txt` внизу страницы не нужны. Устанавливать OpenTTD не нужно: графика, звуки и музыка уже внутри.

**2. Распакуйте и запустите.**
- **Mac:** дважды щёлкните zip, затем `OpenTTD.app`. Сборка не подписана Apple, поэтому в первый раз macOS её не откроет: нажмите *Готово*, откройте *Системные настройки > Конфиденциальность и безопасность*, прокрутите вниз, нажмите у OpenTTD *Всё равно открыть* и подтвердите.
- **Windows:** правой кнопкой по zip, *Извлечь все*, в распакованной папке запустите `openttd.exe`. Если появится синее окно *Система Windows защитила ваш компьютер*, нажмите *Подробнее* и *Выполнить в любом случае*.
- **Linux:** распакуйте (`tar xf ...`) и запустите `./openttd`.

Установленный OpenTTD она не заменяет. Настройки общие.

**3. Поехать, выйти, войти.** Щёлкните по транспорту, в его окне нажмите новую кнопку с кабиной локомотива (справа, под «Детали») — откроется окно **Кабина**. Кнопка **3D** — вид из окна кабины; чтобы осмотреться, удерживайте на виде кнопку мыши, которой прокручиваете карту. Когда поезд или автомобиль стоит, кнопка **Выйти** — пройтись пешком: **стрелки** или **W A S D**, **Shift** — бежать. Рядом со стоящим вагоном или автобусом — **E**: войти, пройти по вагонам, **пробел** — сесть у окна, на следующей остановке **E** — выйти. Рядом с локомотивом **E** сажает в кабину. В консоли (`~`) — `fp help`.

Сохранения этой сборки открываются только в версии OpenTTD в разработке и новее. По сети — только с игроками с той же альфа-версией; пешехода никто, кроме вас, не видит, и поезда перед ним не останавливаются.

Основано на виде из кабины [vaclavloubek/OpenTTD](https://github.com/vaclavloubek/OpenTTD/releases/tag/15.3-cabview-alpha1).
