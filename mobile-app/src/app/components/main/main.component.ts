import { Component, OnInit } from '@angular/core';
import { HttpClient, HttpParams } from '@angular/common/http';

@Component({
  selector: 'app-main',
  templateUrl: './main.component.html',
  styleUrls: ['./main.component.css'],
  providers: [HttpClient]
})
export class MainComponent implements OnInit {
  isAlarmActive: boolean = false;
  isAlarmOn: boolean = false;

  constructor(private http: HttpClient) { }

  ngOnInit(): void {
  }

  getPetition(actionValue: string) {
    let url: string = 'http://192.168.0.104:80/';

    let httpParams = new HttpParams();
    httpParams  = httpParams.append("action", actionValue);

    this.http.get(url, { params: httpParams }).subscribe();
  }

  setAlarm() {
    this.getPetition("set");
  }

  disableAlarm() {
    this.getPetition("disable");
  }

  turnOnAlarm() {
    this.getPetition("turn_on");
  }

  turnOffAlarm() {
    this.getPetition("turn_off");
  }

  calibrateDevice() {
    this.getPetition("calibrate");
  }

}
