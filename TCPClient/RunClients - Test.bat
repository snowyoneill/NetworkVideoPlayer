::-----------------------------------------------------------------------------LOCAL_AUDIO----------------------------------------------------------------------------------------------------
::start ..\x64\Release_LOCALAUDIO\TCPClient.exe -ClientID 1 -StreamID 0 -VideoPath E:\TV\StargateUniverse\Season2\Stargate.Universe.S02E12.HDTV.XviD-ASAP.[VTV].avi -AutoStart false -Loop false -ScreenSync true -Vsync false -Affinity 255 -Debug false -Res 852x480 -Position 0x0 -enablePBOs true -intelliSleep false -multiTimer true
::Timeout  1
::---------------------------------------------------------------------------NETWORKED_AUDIO-------------------------------------------------------------------------------------------------
start ..\x64\Release\TCPClient.exe -UDPPort 2008 -ClientID 1 -StreamID 0 -VideoPath "C:\Users\obscura\Desktop\VJ\Vids\shaun_test_vid_audio.mp4" -AutoStart false -Loop false -ScreenSync true -Vsync true -Affinity 255 -Debug false -Res 852x480 -Position 400x0 -enablePBOs true -intelliSleep false -multiTimer true -Broadcast true -TestLatency false -PTSMaster true
::Timeout  1
start ..\x64\Release\TCPClient.exe -UDPPort 2008 -ClientID 2 -StreamID 0 -VideoPath E:\TV\StargateUniverse\Season2\Stargate.Universe.S02E12.HDTV.XviD-ASAP.[VTV].avi -AutoStart false -Loop false -ScreenSync true -Vsync true -Affinity 255 -Debug false -Res 852x480 -Position 400x480 -enablePBOs true -intelliSleep false -multiTimer true -Broadcast true -TestLatency false -PTSSlave true
::Timeout  1
::start ..\x64\Release\TCPClient.exe -UDPPort 2008 -ClientID 3 -StreamID 0 -VideoPath E:\TV\StargateUniverse\Season2\Stargate.Universe.S02E12.HDTV.XviD-ASAP.[VTV].avi -AutoStart false -Loop false -ScreenSync true -Vsync false -Affinity 255 -Debug false -Res 852x480 -Position 1000x560 -enablePBOs true -intelliSleep false -multiTimer false
::Timeout  1
::start ..\x64\Release\TCPClient.exe -ClientID 3 -StreamID 0 -VideoPath I:\Obscura\PUC\TCPAudioClient\TCPClient\sample4.mp4 -AutoStart false -Loop false -ScreenSync true -Vsync true -Affinity 255 -Debug false -Res 852x480 -Position 0x500 -enablePBOs true -intelliSleep true -multiTimer true
::Timeout  1e
::start ..\x64\Release\TCPClient.exe -ClientID 4 -StreamID 0 -VideoPath I:\Obscura\PUC\TCPAudioClient\TCPClient\sample3.mp4 -AutoStart false -Loop false -ScreenSync true -Vsync true -Affinity 255 -Debug false -Res 852x480 -Position 1000x500 -enablePBOs true -intelliSleep true -multiTimer true
::Timeout  1
::start ..\x64\Release\TCPClient.exe -ClientID 5 -StreamID 0 -VideoPath I:\Obscura\PUC\TCPAudioClient\TCPClient\sample3.mp4 -AutoStart false -Loop false -ScreenSync true -Vsync false -Affinity 1 -Debug false -Res 852x480 -Position 2000x0
::Timeout  1
::start ..\x64\Release\TCPClient.exe -ClientID 6 -StreamID 0 -VideoPath I:\Obscura\PUC\TCPAudioClient\TCPClient\sample1.mp4 -AutoStart false -Loop false -ScreenSync true -Vsync false -Affinity 1 -Debug false -Res 852x480 -Position 2900x0
::Timeout  1
::start ..\x64\Release\TCPClient.exe -ClientID 7 -StreamID 0 -VideoPath I:\Obscura\PUC\TCPAudioClient\TCPClient\sample2.mp4 -AutoStart false -Loop false -ScreenSync true -Vsync false -Affinity 1 -Debug false -Res 852x480 -Position 2000x500
::Timeout  1
::start ..\x64\Release\TCPClient.exe -ClientID 8 -StreamID 0 -VideoPath I:\Obscura\PUC\TCPAudioClient\TCPClient\sample3.mp4 -AutoStart false -Loop false -ScreenSync true -Vsync false -Affinity 1 -Debug false -Res 852x480 -Position 2900x500
::Timeout  1
::start ..\x64\Debug\TCPClient.exe -ClientID 9 -StreamID 0 -VideoPath I:\Obscura\PUC\TCPAudioClient\TCPClient\sample3.mp4 -AutoStart false -Loop false -ScreenSync true -Vsync false -Affinity 1 -Debug false -Res 852x480 -Position 1000x500
::Timeout  1
::start ..\x64\Debug\TCPClient.exe -ClientID 10 -StreamID 0 -VideoPath I:\Obscura\PUC\TCPAudioClient\TCPClient\sample3.mp4 -AutoStart false -Loop false -ScreenSync true -Vsync false -Affinity 1 -Debug false

::--------------------------------------------------------------------------------EXTRA-------------------------------------------------------------------------------------------------------
::start ..\x64\Debug\TCPClient.exe -ClientID 3 -StreamID 0 -VideoPath H:\Obscura\PUC\TCPAudioClient\TCPClient\Serenity - HD DVD Trailer.mp4 2
::Timeout  2
::start ..\x64\Debug\TCPClient.exe -ClientID 4 -StreamID 0 -VideoPath H:\Obscura\PUC\TCPAudioClient\TCPClient\Skyfall - Trailer.mp4 2

::E:\TV\StargateUniverse\Season2\Stargate.Universe.S02E12.HDTV.XviD-ASAP.[VTV].avi

:: ..\x64\Debug_LOCALAUDIO\TCPClient.exe -ClientID 1 -StreamID 0 -VideoPath C:\Users\THoR\Desktop\VJ\Media\Vids\shaun_test_vid_audio.mov -AutoStart false -Loop false -ScreenSync true -Vsync false -Affinity 255 -Debug false -Res 852x480 -Position 0x0 -enablePBOs true -intelliSleep true -multiTimer false