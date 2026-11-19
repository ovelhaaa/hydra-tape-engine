"""Static, shareable Phase 6.2 figures. Read existing measurements, never rerender."""
import json
from pathlib import Path
import numpy as np
from scipy import signal
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/bias-antialiasing'


def main():
    results=json.loads((OUT/'results.json').read_text())
    spectra=np.load(OUT/'spectra.npz')
    plots=OUT/'plots'; plots.mkdir(exist_ok=True)
    for rate in (44100,48000,96000):
        fig,axes=plt.subplots(3,2,figsize=(13,10),layout='constrained')
        ranges=((20,20000),(2800,3200),(5800,6200),(9800,12200),(17600,18400),(1000,2200))
        for ax,(lo,hi) in zip(axes.flat,ranges):
            for name in results['variants']:
                key=f'{name}_{rate}_sine440'
                if key not in spectra: continue
                psd=spectra[key]; f=np.arange(len(psd))
                ax.plot(f,10*np.log10(np.maximum(psd,1e-30)),label=name,linewidth=.85)
            ax.set(xlim=(lo,hi),ylim=(-180,-10),xlabel='Hz',ylabel='dBFS/Hz',title=f'{rate} Hz, {lo}–{hi} Hz')
            ax.grid(alpha=.2)
        axes[0,0].legend(fontsize=8)
        fig.suptitle('Isolated magnetic stage, 440 Hz / −20 dBFS input; Welch Hann 1 s / 50% overlap')
        fig.savefig(plots/f'psd_{rate}.png',dpi=150); plt.close(fig)
    fig,axes=plt.subplots(2,2,figsize=(12,8),layout='constrained')
    for name,v in results['variants'].items():
        curve=v['transfer']['driven_aged_slow']; x=np.array(curve['input']); y=np.array(curve['output'])
        axes[0,0].plot(x,y,label=name); axes[0,1].plot(x,curve['slope'],label=name)
    for name in ('baseline','averaged'):
        y=np.load(OUT/name/'48000_impulse.npy')
        axes[1,0].plot(np.arange(len(y))/48000,y,label=name)
        y=np.load(OUT/name/'48000_guitar_clean.npy')
        f,t,p=signal.spectrogram(y,48000,nperseg=4096,noverlap=3072,window='hann')
        # Two spectra overlaid as 1s FFTs separately in companion figure below.
    axes[0,0].set(title='DC plateau transfer: drive100 age100 speed0',xlabel='input',ylabel='mean output')
    axes[0,1].set(title='Incremental gain',xlabel='input',ylabel='dy/dx')
    axes[1,0].set(title='Impulse response / magnetic memory',xlabel='seconds',xlim=(.499,.515))
    for name,v in results['variants'].items():
        axes[1,1].bar(name,v['native_magnetic_us_per_stereo_block']['mean'])
    axes[1,1].set(title='Native mean magnetic stage cost (not ESP32)',ylabel='µs / stereo128 frames')
    axes[1,1].tick_params(axis='x',rotation=30)
    axes[0,0].legend(fontsize=8); axes[1,0].legend(fontsize=8)
    fig.savefig(plots/'transfer_transients_cost.png',dpi=150); plt.close(fig)
    fig,axes=plt.subplots(2,2,figsize=(13,8),layout='constrained')
    for row,name in enumerate(('baseline','averaged')):
        y=np.load(OUT/name/'48000_guitar_clean.npy')
        f,t,p=signal.spectrogram(y,48000,nperseg=4096,noverlap=3072,window='hann')
        axes[row,0].pcolormesh(t,f,10*np.log10(np.maximum(p,1e-30)),vmin=-120,vmax=-30,shading='auto')
        axes[row,0].set(ylim=(0,20000),title=name+' guitar spectrogram',xlabel='seconds',ylabel='Hz')
        segment=y[48000:96000]*signal.windows.hann(48000,sym=False)
        amplitude=np.abs(np.fft.rfft(segment))/(48000/4)
        axes[row,1].plot(np.fft.rfftfreq(len(segment),1/48000),20*np.log10(np.maximum(amplitude,1e-15)))
        axes[row,1].set(xlim=(0,20000),ylim=(-160,0),title=name+' guitar FFT, Hann1s coherent amplitude',xlabel='Hz',ylabel='dBFS')
    fig.savefig(plots/'fft_spectrogram.png',dpi=150); plt.close(fig)
    engine=OUT/'engine-results.json'
    if engine.exists():
        data=json.loads(engine.read_text()); fig,axes=plt.subplots(1,2,figsize=(12,4),layout='constrained')
        for ax,rate in zip(axes,('44100','48000')):
            for case in ('feedback95','reverse','multihead'):
                for name,entry in data['rates'][rate][case].items():
                    ax.plot(np.arange(6)+.5,entry['rms_by_second_dbfs'],label=f'{case}/{name}')
            ax.set(title=rate+' Hz full engine tail; input stops1s',xlabel='seconds',ylabel='RMS dBFS'); ax.legend(fontsize=7)
        fig.savefig(plots/'feedback_decay.png',dpi=150); plt.close(fig)
    print('Figures:',plots)


if __name__=='__main__': main()
