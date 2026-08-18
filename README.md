# HGTD VTRx+ Local Mock Demo

Lokalna Windows demonstraciona aplikacija za VTRx+ kontrolnu arhitekturu. Implementirani su lifecycle kontrolera, simulirani register READ/WRITE, dijagnostika operacija i bezbedno gašenje; monitoring vrednosti i register banka jasno su označeni kao MOCK/SIMULATION i aplikacija ne koristi CERN hardver niti OPC UA endpoint.

## Preuzimanje i pokretanje

```powershell
git clone https://github.com/poni0107/Hgtd_Vtrxp_OPCUA_LOCAL_DEMO.git
cd Hgtd_Vtrxp_OPCUA_LOCAL_DEMO
.\Launch-VtrxpMockGui.cmd --demo-ready
```

Može se pokrenuti i direktno preko `vtrxp_mock_gui.exe`. Za ponovno kompajliranje potreban je Visual Studio 2022 sa C++ podrškom, zatim se pokreće `BuildFromSource.cmd`.
